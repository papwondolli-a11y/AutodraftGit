use acadrust::{DwgReader, DxfVersion, DxfWriter};
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
    eprintln!("usage: dwg2dxf <in.dwg> <out.dxf> [--version r12|2000|2004|2007|2010|2013|2018]");
}

fn run() -> Result<(), String> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let mut positional: Vec<&String> = Vec::new();
    let mut version: Option<DxfVersion> = None;

    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--version" => {
                i += 1;
                let v = args.get(i).ok_or("--version needs a value")?;
                version = Some(parse_version(v).ok_or(format!("unknown version: {v}"))?);
            }
            _ => positional.push(&args[i]),
        }
        i += 1;
    }
    if positional.len() != 2 {
        usage();
        return Err("expected input and output paths".into());
    }

    let doc = DwgReader::from_file(positional[0])
        .and_then(|mut r| r.read())
        .map_err(|e| format!("read failed: {e}"))?;

    println!("read: {} entities, source version {:?}", doc.entities().count(), doc.version);
    let mut writer = DxfWriter::new(&doc);
    if let Some(v) = version {
        writer = writer.with_version(v);
    }
    writer
        .write_to_file(positional[1])
        .map_err(|e| format!("write failed: {e}"))?;
    println!("wrote: {}", positional[1]);
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
