use acadrust::{DwgWriter, DxfReader, DxfReaderConfiguration, DxfVersion};
use std::process::ExitCode;

fn parse_version(s: &str) -> Option<DxfVersion> {
    match s {
        "r12" => Some(DxfVersion::AC1009),
        "2000" => Some(DxfVersion::AC1015),
        "2004" => Some(DxfVersion::AC1018),
        "2007" => Some(DxfVersion::AC1021),
        "2010" => Some(DxfVersion::AC1024),
        "2013" => Some(DxfVersion::AC1027),
        "2018" => Some(DxfVersion::AC1032),
        _ => None,
    }
}

fn usage() {
    eprintln!("usage: dxf2dwg <in.dxf> <out.dwg> [--version r12|2000|2004|2007|2010|2013|2018] [--failsafe]");
}

fn run() -> Result<(), String> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let mut positional: Vec<&String> = Vec::new();
    let mut version: Option<DxfVersion> = None;
    let mut failsafe = false;

    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--version" => {
                i += 1;
                let v = args.get(i).ok_or("--version needs a value")?;
                version = Some(parse_version(v).ok_or(format!("unknown version: {v}"))?);
            }
            "--failsafe" => failsafe = true,
            _ => positional.push(&args[i]),
        }
        i += 1;
    }
    if positional.len() != 2 {
        usage();
        return Err("expected input and output paths".into());
    }

    let config = DxfReaderConfiguration {
        failsafe,
        ..Default::default()
    };
    let outcome = DxfReader::from_file(positional[0])
        .map_err(|e| format!("open failed: {e}"))?
        .with_configuration(config)
        .read_with_stats()
        .map_err(|e| format!("read failed: {e}"))?;
    let mut doc = outcome.document;

    println!("read: {} entities, source version {:?}", doc.entities().count(), doc.version);
    if let Some(v) = version {
        doc.version = v;
    }
    DwgWriter::write_to_file(positional[1], &doc).map_err(|e| format!("write failed: {e}"))?;
    println!("wrote: {} (version {:?})", positional[1], doc.version);
    Ok(())
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::FAILURE
        }
    }
}
