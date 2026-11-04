#include "appcommandregistry.h"
#include "angulardimensiontoolA.h"
#include "arc3pointtool.h"
#include "arccentertool.h"
#include "arraytool.h"
#include "baselinedimensiontool.h"
#include "cadcommands.h"
#include "circletool.h"
#include "commandbase.h"
#include "commandfactory.h"
#include "commandmanager.h"
#include "continuedimensiontool.h"
#include "copytool.h"
#include "dimstylecommand.h"
#include "drawingsettingscommand.h"
#include "erasetool.h"
#include "jointool.h"
#include "explodetool.h"
#include "extendtool.h"
#include "filecommands.h"
#include "templatecommands.h"
#include "fillettool.h"
#include "gdttool.h"
#include "surfacefinishtool.h"
#include "halfdimensiontool.h"
#include "areadimensiontool.h"
#include "hatchtool.h"
#include "holetool.h"
#include "holeptool.h"
#include "holesectiontool.h"
#include "holecallouttool.h"
#include "overkillcommand.h"
#include "toolcommand.h"
//
#include "AngularDimensionTool.h"
#include "RadialDimensionTool.h"
#include "chamfertool.h"
#include "datumtool.h"
#include "dimleadertool.h"
#include "dimtextedittool.h"
#include "dtexttool.h"
#include "boxtexttool.h"
#include "gdtedittool.h"
#include "leadertool.h"
#include "lineardimensiontool.h"
#include "linetool.h"
#include "twolinetool.h"
//#include "twolinesetupdialog.h"
#include "multilinecornertool.h"
#include "multilinetcornertool.h"
#include "multilineraycastcornertool.h"
#include "multilineraycastlcornertool.h"
#include "multilineccornertool.h"
#include "areacornerjointool.h"
#include "doorlayerdialog.h"
#include "walltypedialog.h"
#include "plandoortool.h"
#include "plandoordoubletool.h"
#include "plandoorbiswingtool.h"
#include "plandoordoublebiswingtool.h"
#include "planrevolvingdoortool.h"
#include "plandoorsimpletool.h"
#include "planwindowtool.h"
#include "windowsettingsdialog.h"
#include "movetool.h"
#include "newsubwindowcommand.h"
#include "offsettool.h"
#include "polylinetool.h"
#include "texttool.h"
#include "toolcommand.h"
#include "trimtool.h"
#include "wblocktool.h"
#include "dimlineartool.h"
#include "autocontinuousdimtool.h"
#include "gridblubbletool.h"
#include "dimmovetexttool.h"
#include "dimmergetool.h"
#include "dimsplittool.h"
#include "severtool.h"
#include "parallelcentertool.h"
#include "boxcentertool.h"
#include "gridcentertool.h"
#include "circlecentertool.h"
#include "notimplementedcommand.h"
//#include "bolttool.h"
#include "wrenchbolttool.h"
#include "hhbolttool.h"
#include "hbbolttool.h"
#include "boltpickerdialog.h"
#include "polygontool.h"
#include "stairplantool.h"
#include "rubbletool.h"
#include "elevatortool.h"
#include "columntool.h"
#include "beamscheduletool.h"
#include "beamsigntool.h"
#include "ribtool.h"
#include "insulationtool.h"
#include "ellipsetool.h"
#include "detailviewtool.h"


AppCommandRegistry::AppCommandRegistry(QObject *parent) : QObject(parent) {}

void AppCommandRegistry::registerAllCommands(CommandManager *mgr,
                                             IAppContext *ctx,
                                             CommandFactory &factory) {
  if (!mgr || !ctx)
    return;

  // <1. 명령어 생성자 등록 >=======================================
  // 커스텀 타이틀바 uno/redo
  // ------------------------------------------------------------------
  factory.registerCommand(
      "edit.undo", [](IAppContext *c) { return new GlobalUndoCommand(c); });
  factory.registerCommand(
      "edit.redo", [](IAppContext *c) { return new GlobalRedoCommand(c); });

  // 메뉴바 파일 등록 명령어 -----------------------------------
  factory.registerCommand(
      "file.new", [](IAppContext *c) { return new NewSubWindowCommand(c); });
  factory.registerCommand("file.newfromtemplate", [](IAppContext *c) {
    return new NewFromTemplateCommand(c);
  });
  factory.registerCommand("file.saveastemplate", [](IAppContext *c) {
    return new SaveAsTemplateCommand(c);
  });
  factory.registerCommand(
      "file.open", [](IAppContext *c) { return new FileOpenCommand(c); });
  factory.registerCommand(
      "file.save", [](IAppContext *c) { return new FileSaveCommand(c); });
  factory.registerCommand(
      "file.saveas", [](IAppContext *c) { return new FileSaveAsCommand(c); });
  factory.registerCommand(
      "file.batchprint", [](IAppContext *c) { return new FileBatchPrintCommand(c); });
  factory.registerCommand(
      "file.printstyle", [](IAppContext *c) { return new PrintStyleCommand(c); });
  factory.registerCommand(
      "file.dxfimport", [](IAppContext *c) { return new FileDxfImportCommand(c); });
  factory.registerCommand(
      "file.dxfexport", [](IAppContext *c) { return new FileDxfExportCommand(c); });
  factory.registerCommand(
      "file.dwgimport", [](IAppContext *c) { return new FileDwgImportCommand(c); });
  factory.registerCommand(
      "file.dwgexport", [](IAppContext *c) { return new FileDwgExportCommand(c); });
  factory.registerCommand("file.exit",
                          [](IAppContext *c) { return new ExitCommand(c); });

  // 탭메뉴 multiline 등록 명령어 -----------------------------------
  factory.registerCommand("multiline.twoline", [](IAppContext *c) {
      return new ToolCommand<TwoLineTool>(c);
  });
  // factory.registerCommand("multiline.twoline.setup", [](IAppContext *c) {
  //     return new TwoLineSetupCommand(c);
  // });
  factory.registerCommand("multiline.fourline", [](IAppContext *c) {
      return new ToolCommand<FourLineTool>(c);
  });
  // factory.registerCommand("multiline.fourline.setup", [](IAppContext *c) {
  //     return new TwoLineSetupCommand(c, true, false);
  // });
  factory.registerCommand("multiline.sixline", [](IAppContext *c) {
      return new ToolCommand<SixLineTool>(c);
  });
  // factory.registerCommand("multiline.sixline.setup", [](IAppContext *c) {
  //     return new TwoLineSetupCommand(c, true, true);
  // });
  factory.registerCommand("multiline.lcorner", [](IAppContext *c) {
      return new MultilineCornerCommand(c);
  });
  factory.registerCommand("multiline.raycast_lcorner", [](IAppContext *c) {
      return new MultilineRaycastLCornerCommand(c);
  });
  factory.registerCommand("multiline.tcorner", [](IAppContext *c) {
      return new MultilineTCornerCommand(c);
  });
  factory.registerCommand("multiline.raycast_tcorner", [](IAppContext *c) {
      return new MultilineRaycastTCornerCommand(c);
  });
  factory.registerCommand("multiline.ccorner", [](IAppContext *c) {
      return new MultilineCCornerCommand(c);
  });
  factory.registerCommand("multiline.area_lcorner", [](IAppContext *c) {
      return new AreaCornerJoinCommand(c, AreaCornerJoinTool::LCorner);
  });
  factory.registerCommand("multiline.area_tcorner", [](IAppContext *c) {
      return new AreaCornerJoinCommand(c, AreaCornerJoinTool::TCorner);
  });
  factory.registerCommand("multiline.area_ccorner", [](IAppContext *c) {
      return new AreaCornerJoinCommand(c, AreaCornerJoinTool::CCorner);
  });
  factory.registerCommand("multiline.eccentrictwoline", [](IAppContext *c) {
      return new ToolCommand<EccentricTwoLineTool>(c);
  });
  factory.registerCommand("multiline.eccentricfourline", [](IAppContext *c) {
      return new ToolCommand<EccentricFourLineTool>(c);
  });
  factory.registerCommand("multiline.eccentricsixline", [](IAppContext *c) {
      return new ToolCommand<EccentricSixLineTool>(c);
  });

  // 탭메뉴 draw 등록 명려어  ---------------------------------------
  factory.registerCommand(
      "draw.line", [](IAppContext *c) { return new ToolCommand<LineTool>(c); });
  factory.registerCommand("draw.circle", [](IAppContext *c) {
    return new ToolCommand<CircleTool>(c);
  });
  factory.registerCommand("draw.arc", [](IAppContext *c) {
    return new ToolCommand<ArcCenterTool>(c);
  });
  factory.registerCommand("draw.arc3point", [](IAppContext *c) {
    return new ToolCommand<Arc3PointTool>(c);
  });
  factory.registerCommand("draw.polyline", [](IAppContext *c) {
    return new ToolCommand<PolylineTool>(c);
  });
  factory.registerCommand("draw.polygon", [](IAppContext *c) {
    return new ToolCommand<PolygonTool>(c);
  });
  factory.registerCommand("draw.hatch", [](IAppContext *c) {
    return new ToolCommand<HatchTool>(c);
  });
  factory.registerCommand("draw.spline", [](IAppContext *c) {
    return new SplineCommand(c);
  });
  factory.registerCommand("draw.ellipse", [](IAppContext *c) {
    return new ToolCommand<EllipseTool>(c);
  });
  factory.registerCommand("draw.parallelcenter", [](IAppContext *c) {
    return new ToolCommand<ParallelCenterTool>(c);
  });
  factory.registerCommand("draw.boxcenter", [](IAppContext *c) {
    return new ToolCommand<BoxCenterTool>(c);
  });
  factory.registerCommand("draw.gridcenter", [](IAppContext *c) {
    return new ToolCommand<GridCenterTool>(c);
  });
  factory.registerCommand("draw.circlecenter", [](IAppContext *c) {
    return new ToolCommand<CircleCenterTool>(c);
  });
  factory.registerCommand("draw.gridbubble", [](IAppContext *c) {
    return new ToolCommand<GridBubbleTool>(c);
  });
  factory.registerCommand("draw.hole", [](IAppContext *c) {
    return new ToolCommand<HoleTool>(c);
  });
  factory.registerCommand("draw.holesection", [](IAppContext *c) {
    return new ToolCommand<HoleSectionTool>(c);
  });
  factory.registerCommand("draw.holep", [](IAppContext *c) {
    return new ToolCommand<HolePTool>(c);
  });
  factory.registerCommand("draw.holecallout", [](IAppContext *c) {
    return new ToolCommand<HoleCalloutTool>(c);
  });

  // 탭메뉴 modify 등록 명령어 -----------------------------------
  factory.registerCommand("modify.move", [](IAppContext *c) {
    return new ToolCommand<MoveTool>(c);
  });
  factory.registerCommand("modify.copy", [](IAppContext *c) {
    return new ToolCommand<CopyTool>(c);
  });
  factory.registerCommand("modify.erase", [](IAppContext *c) {
    return new ToolCommand<EraseTool>(c);
  });
  factory.registerCommand("modify.rotate",
                          [](IAppContext *c) { return new RotateCommand(c); });
  factory.registerCommand("modify.scale",
                          [](IAppContext *c) { return new ScaleCommand(c); });
  factory.registerCommand("modify.mirror",
                          [](IAppContext *c) { return new MirrorCommand(c); });
  factory.registerCommand("modify.quadmirror", [](IAppContext *c) {
    return new QuadMirrorCommand(c);
  });
  factory.registerCommand("modify.offset", [](IAppContext *c) {
    return new ToolCommand<OffsetTool>(c);
  });
  factory.registerCommand("modify.trim", [](IAppContext *c) {
    return new ToolCommand<TrimTool>(c);
  });
  factory.registerCommand("modify.extend", [](IAppContext *c) {
    return new ToolCommand<ExtendTool>(c);
  });
  factory.registerCommand("modify.fillet", [](IAppContext *c) {
    return new ToolCommand<FilletTool>(c);
  });
  factory.registerCommand("modify.chamfer", [](IAppContext *c) {
    return new ToolCommand<ChamferTool>(c);
  });
  factory.registerCommand("modify.array", [](IAppContext *c) {
    return new ToolCommand<ArrayTool>(c);
  });
  factory.registerCommand("modify.explode", [](IAppContext *c) {
    return new ToolCommand<ExplodeTool>(c);
  });
  factory.registerCommand("modify.stretch",
                          [](IAppContext *c) { return new StretchCommand(c); });
  factory.registerCommand(
      "modify.overkill", [](IAppContext *c) { return new OverkillCommand(c); });
  factory.registerCommand("modify.sever", [](IAppContext *c) {
    return new ToolCommand<SeverTool>(c);
  });
  factory.registerCommand("modify.join", [](IAppContext *c) {
    return new ToolCommand<JoinTool>(c);
  });
  factory.registerCommand("modify.detailview", [](IAppContext *c) {
    return new ToolCommand<DetailViewTool>(c);
  });
  // 탭메뉴 view 등록 명령어
  // --------------------------------------------------------------
  factory.registerCommand("view.zoomall",
                          [](IAppContext *c) { return new ZoomAllCommand(c); });
  factory.registerCommand("view.zoomextend", [](IAppContext *c) {
    return new ZoomExtendCommand(c);
  });
  factory.registerCommand("view.zoomwindow", [](IAppContext *c) {
    return new ZoomWindowCommand(c);
  });
  factory.registerCommand("view.limits", [](IAppContext *c) {
    return new DrawingSettingsCommand(c);
  });

  // 탭메뉴 text 등록 명령어
  // --------------------------------------------------------------
  factory.registerCommand(
      "text.text", [](IAppContext *c) { return new ToolCommand<TextTool>(c); });
  factory.registerCommand("text.dtext", [](IAppContext *c) {
    return new ToolCommand<DTextTool>(c);
  });
  factory.registerCommand("text.boxtext", [](IAppContext *c) {
    return new ToolCommand<BoxTextTool>(c);
  });

  // 탭메뉴 block 등록 명령어
  // ------------------------------------------------------------------
  factory.registerCommand(
      "block.insert", [](IAppContext *c) { return new InsertCallCommand(c); });
  factory.registerCommand("block.wblock", [](IAppContext *c) {
    return new ToolCommand<WBlockTool>(c);
  });
  factory.registerCommand("block.lib.furniture", [](IAppContext *c) {
    return new BlockLibraryCommand(c, "furniture");
  });
  factory.registerCommand("block.lib.sink", [](IAppContext *c) {
    return new BlockLibraryCommand(c, "sink");
  });
  factory.registerCommand("block.lib.toilet", [](IAppContext *c) {
    return new BlockLibraryCommand(c, "toilet");
  });
  factory.registerCommand("block.lib.tree", [](IAppContext *c) {
    return new BlockLibraryCommand(c, "tree");
  });
  factory.registerCommand("block.lib.elev", [](IAppContext *c) {
      return new BlockLibraryCommand(c, "elevator");
  });


  // 탭메뉴 기계부품 명령어 등록
  // ------------------------------------------------------------------
  // 아직 실제 그리기 Tool이 없는 항목은 NotImplementedCommand로 자리만
  // 잡아둔다. 항목별 구현이 끝나면 여기서 ToolCommand<해당Tool>로 교체한다.
  factory.registerCommand("mech.bolt", [](IAppContext *c) {
    return new BoltPickerCommand(c);
  });
  factory.registerCommand("mech.wrenchbolt", [](IAppContext *c) {
    return new ToolCommand<WrenchBoltTool>(c);
  });
  factory.registerCommand("mech.hhbolt", [](IAppContext *c) {
    return new ToolCommand<HHBoltTool>(c);
  });
  factory.registerCommand("mech.hbbolt", [](IAppContext *c) {
    return new ToolCommand<HBBoltTool>(c);
  });
  factory.registerCommand("mech.nut", [](IAppContext *c) {
    return new NotImplementedCommand(c, tr("너트"));
  });
  factory.registerCommand("mech.washer", [](IAppContext *c) {
    return new NotImplementedCommand(c, tr("와셔"));
  });
  factory.registerCommand("mech.key", [](IAppContext *c) {
    return new NotImplementedCommand(c, tr("키"));
  });
  factory.registerCommand("mech.pin", [](IAppContext *c) {
    return new NotImplementedCommand(c, tr("핀"));
  });
  factory.registerCommand("mech.bearing", [](IAppContext *c) {
    return new NotImplementedCommand(c, tr("베어링"));
  });
  factory.registerCommand("mech.spring", [](IAppContext *c) {
    return new NotImplementedCommand(c, tr("스프링"));
  });
  factory.registerCommand("mech.retainingring", [](IAppContext *c) {
    return new NotImplementedCommand(c, tr("리테이닝링"));
  });
  factory.registerCommand("mech.structuralsteel", [](IAppContext *c) {
    return new NotImplementedCommand(c, tr("구조형강"));
  });

  // 탭메뉴 dimension 명령어 등록
  // --------------------------------------------------------
  //  factory.registerCommand("draw.dimlinear", [](IAppContext *c) {
  //    return new ToolCommand<DimensionTool>(c);});

  factory.registerCommand("draw.linearDim", [](IAppContext *c) {
    return new ToolCommand<LinearDimensionTool>(c);
  });

  factory.registerCommand("draw.dimlinear_hv", [](IAppContext *c) {
      return new ToolCommand<DimLinearTool>(c);
  });

  factory.registerCommand("draw.dimbaseline", [](IAppContext *c) {
    return new ToolCommand<BaselineDimensionTool>(c);
  });

  factory.registerCommand("draw.dimcontinue", [](IAppContext *c) {
    return new ToolCommand<ContinuedimensionTool>(c);
  });

  factory.registerCommand("draw.autocontinuousdim", [](IAppContext *c) {
    return new ToolCommand<AutoContinuousDimTool>(c);
  });
  factory.registerCommand(
      "draw.dimstyle", [](IAppContext *c) { return new DimStyleCommand(c); });
  factory.registerCommand("draw.dimangular", [](IAppContext *c) {
    return new ToolCommand<AngularDimensionToolA>(c);
  });
  factory.registerCommand("draw.leader", [](IAppContext *c) {
    return new ToolCommand<LeaderTool>(c);
  });
  factory.registerCommand("draw.halfdim", [](IAppContext *c) {
    return new ToolCommand<HalfDimensionTool>(c);
  });
  factory.registerCommand("draw.areadim", [](IAppContext *c) {
    return new ToolCommand<AreaDimensionTool>(c);
  });
  factory.registerCommand("draw.radDim", [](IAppContext *c) {
    return new ToolCommand<RadialDimensionTool>(c);
  });
  factory.registerCommand("draw.angDim", [](IAppContext *c) {
    return new ToolCommand<AngularDimensionTool>(c);
  });
  factory.registerCommand("draw.dimlea", [](IAppContext *c) {
    return new ToolCommand<DimLeaderTool>(c);
  });
  factory.registerCommand("draw.dimTextEdit", [](IAppContext *c) {
    return new ToolCommand<DimTextEditTool>(c);
  });
  factory.registerCommand("draw.dimMoveText", [](IAppContext *c) {
      return new ToolCommand<DimMoveTextTool>(c);
  });
  factory.registerCommand("draw.dimMerge", [](IAppContext *c) {
      return new ToolCommand<DimMergeTool>(c);
  });
  factory.registerCommand("draw.dimSplit", [](IAppContext *c) {
      return new ToolCommand<DimSplitTool>(c);
  });

  // [GDT 추가] 기하공차 프레임 도구 등록
  factory.registerCommand(
      "draw.gdt", [](IAppContext *c) { return new ToolCommand<GdtTool>(c); });
  factory.registerCommand("draw.datum", [](IAppContext *c) {
    return new ToolCommand<DatumTool>(c);
  });
  factory.registerCommand("draw.gdtedittool", [](IAppContext *c) {
    return new ToolCommand<GdtEditTool>(c);
  });

  // 표면 거칠기 기호 도구 등록
  factory.registerCommand("draw.surfacefinish", [](IAppContext *c) {
    return new ToolCommand<SurfaceFinishTool>(c);
  });

  // 2. <명령어 등록 (UI 정보 결합)> =====================================
  // 메뉴바 파일 등록 명령어 -----------------------------------
  {
    CommandInfo info(QStringLiteral("menu.file.new"), tr("파일"), tr("새로만들기"),
                     QString(":/icons/new.png"), QStringLiteral("Ctrl+N"));
    mgr->registerCommand(factory.createCommand("file.new", ctx), info, "new");
  }

  {
    CommandInfo info(QStringLiteral("menu.file.newfromtemplate"), tr("파일"),
                     tr("템플릿 보기..."), QString(":/icons/new.png"),
                     QStringLiteral("Ctrl+Shift+N"));
    mgr->registerCommand(factory.createCommand("file.newfromtemplate", ctx), info,
                         "newfromtemplate");
  }

  {
    CommandInfo info(QStringLiteral("menu.file.saveastemplate"), tr("파일"),
                     tr("템플릿으로 저장..."), QString(":/icons/saveas.png"),
                     QStringLiteral(""));
    mgr->registerCommand(factory.createCommand("file.saveastemplate", ctx), info,
                         "saveastemplate");
  }

  {
    CommandInfo info(QStringLiteral("menu.file.open"), tr("파일"), tr("열기"),
                     QString(":/icons/open.png"), QStringLiteral("Ctrl+O"));
    CommandBase *cmd = factory.createCommand("file.open", ctx);
    mgr->registerCommand(cmd, info, "open");
  }

  {
    CommandInfo info(QStringLiteral("menu.file.save"), tr("파일"), tr("저장하기"),
                     QString(":/icons/save.png"), QStringLiteral("Ctrl+S"));
    CommandBase *cmd = factory.createCommand("file.save", ctx);
    mgr->registerCommand(cmd, info, "save");
  }

  {
    CommandInfo info(QStringLiteral("menu.file.saveas"), tr("파일"),
                     tr("다른이름 저장하기"), QString(":/icons/saveas.png"),
                     QStringLiteral("Ctrl+Shift+S"));
    CommandBase *cmd = factory.createCommand("file.saveas", ctx);
    mgr->registerCommand(cmd, info, "saveas");
  }

  {
    CommandInfo info(QStringLiteral("menu.file.batchprint"), tr("파일"),
                     tr("일괄 출력..."), QString(":/icons/fileprint.png"),
                     QStringLiteral(""));
    CommandBase *cmd = factory.createCommand("file.batchprint", ctx);
    mgr->registerCommand(cmd, info, "batchprint");
  }

  {
    CommandInfo info(QStringLiteral("menu.file.printstyle"), tr("파일"),
                     tr("출력 스타일 편집..."), QString(":/icons/fileprint.png"),
                     QStringLiteral(""));
    CommandBase *cmd = factory.createCommand("file.printstyle", ctx);
    mgr->registerCommand(cmd, info, "printstyle");
  }

  {
    CommandInfo info(QStringLiteral("menu.file.dxfimport"), tr("파일"), tr("DXF 가져오기..."),
                     QString(":/icons/open.png"), QStringLiteral(""));
    CommandBase *cmd = factory.createCommand("file.dxfimport", ctx);
    mgr->registerCommand(cmd, info, "dxfimport");
  }

  {
    CommandInfo info(QStringLiteral("menu.file.dxfexport"), tr("파일"), tr("DXF 내보내기..."),
                     QString(":/icons/save.png"), QStringLiteral(""));
    CommandBase *cmd = factory.createCommand("file.dxfexport", ctx);
    mgr->registerCommand(cmd, info, "dxfexport");
  }

  {
    CommandInfo info(QStringLiteral("menu.file.dwgimport"), tr("파일"), tr("DWG 가져오기..."),
                     QString(":/icons/open.png"), QStringLiteral(""));
    CommandBase *cmd = factory.createCommand("file.dwgimport", ctx);
    mgr->registerCommand(cmd, info, "dwgimport");
  }

  {
    CommandInfo info(QStringLiteral("menu.file.dwgexport"), tr("파일"), tr("DWG로 저장..."),
                     QString(":/icons/save.png"), QStringLiteral(""));
    CommandBase *cmd = factory.createCommand("file.dwgexport", ctx);
    mgr->registerCommand(cmd, info, "dwgexport");
  }

  {
    CommandInfo info(QStringLiteral("menu.file.exit"), tr("파일"), tr("끝내기"),
                     QString(":/icons/exit.png"), QStringLiteral("Alt+F4"));
    CommandBase *cmd = factory.createCommand("file.exit", ctx);
    mgr->registerCommand(cmd, info, "exit");
  }

  // 메뉴바 설정 등록 -----------------------------------------
  // 보기탭 "도면틀"(view.limits)·치수선탭 "스타일"(draw.dimstyle) 명령을
  // 메뉴바 "설정"에도 노출한다. 탭 버튼은 그대로 유지되며, 여기서는 별도
  // 명령 인스턴스를 만들어 같은 다이얼로그를 연다.
  {
    CommandInfo info(QStringLiteral("menu.settings.drawingsettings"), tr("설정"),
                     tr("도면설정"), QString(":/icons/limits.png"),
                     QStringLiteral(""));
    mgr->registerCommand(factory.createCommand("view.limits", ctx), info,
                         QString());
  }

  {
    CommandInfo info(QStringLiteral("menu.settings.dimstyle"), tr("설정"),
                     tr("치수 설정"), QString(":/icons/dimstyle.png"),
                     QStringLiteral(""));
    mgr->registerCommand(factory.createCommand("draw.dimstyle", ctx), info,
                         QString());
  }

  // 탭바 draw
  // ---------------------------------------------------------------------------------------
  {
    CommandInfo info(QStringLiteral("tab.draw.line"), tr("그리기"), tr("  라인  "),
                     QString(":/icons/lineItem.png"), QStringLiteral("li"));
    mgr->registerCommand(factory.createCommand("draw.line", ctx), info, "line");
  }

  {
    CommandInfo info(QStringLiteral("tab.draw.circle"), tr("그리기"),
                     tr("   원   "), QString(":/icons/circleItem.png"),
                     QStringLiteral("ci"));
    mgr->registerCommand(factory.createCommand("draw.circle", ctx), info,
                         "circle");
  }

  {
    CommandInfo info(QStringLiteral("tab.draw.arccenter"), tr("그리기"),
                     tr("   호   "), QString(":/icons/arcItem.png"),
                     QStringLiteral("ar"));
    mgr->registerCommand(factory.createCommand("draw.arc", ctx), info, "arc");
  }

  {
    CommandInfo info(QStringLiteral("tab.draw.arc3point"), tr("그리기"),
                     tr(" 3점 호 "), QString(":/icons/arc3Item.png"),
                     QStringLiteral("a3"));
    mgr->registerCommand(factory.createCommand("draw.arc3point", ctx), info, "arc3p");
  }

  {
    CommandInfo info(QStringLiteral("tab.draw.polyline"), tr("그리기"),
                     tr("폴리라인"), QString(":/icons/polyline.png"),
                     QStringLiteral("po"));
    mgr->registerCommand(factory.createCommand("draw.polyline", ctx), info,
                         "pline");
  }

  {
    CommandInfo info(QStringLiteral("tab.draw.polygon"), tr("그리기"),
                     tr(" 다각형 "), QString(":/icons/polygon.png"), QStringLiteral("pg"));
    mgr->registerCommand(factory.createCommand("draw.polygon", ctx), info,
                         "polygon");
  }

  {
    CommandInfo info(QStringLiteral("tab.draw.hatch"), tr("그리기"), tr("  해칭  "),
                     QString(":/icons/hatch.png"), QStringLiteral("ha"));
    mgr->registerCommand(factory.createCommand("draw.hatch", ctx), info,
                         "hatch");
  }

  {
    CommandInfo info(QStringLiteral("tab.draw.spline"), tr("그리기"), tr("스플라인"),
                     QString(":/icons/spline.png"), QStringLiteral("spl"));
    mgr->registerCommand(factory.createCommand("draw.spline", ctx), info,
                         "spline");
  }

  {
    CommandInfo info(QStringLiteral("tab.draw.ellipse"), tr("그리기"), tr("  타원  "),
                     QString(":/icons/ellipse.png"), QStringLiteral("ell"));
    mgr->registerCommand(factory.createCommand("draw.ellipse", ctx), info,
                         "ellipse");
  }

  // {
  //     CommandInfo info(QStringLiteral("tab.draw.gridcenter"), tr("그리기"),
  //                      tr("멀티중심선 "), QString(":/icons/mcenter.png"),
  //                      QStringLiteral("cl"));
  //     mgr->registerCommand(factory.createCommand("draw.gridcenter", ctx), info,
  //                          "mcenterline");
  // }

  {
    CommandInfo info(QStringLiteral("tab.draw.parallelcenter"), tr("그리기"),
                     tr("평행중심"), QString(":/icons/pcenter.png"),
                     QStringLiteral("pc"));
    mgr->registerCommand(factory.createCommand("draw.parallelcenter", ctx), info,
                         "parallelcenter");
  }

  {
    CommandInfo info(QStringLiteral("tab.draw.boxcenter"), tr("그리기"),
                     tr("박스 중심선"), QString(":/icons/bcenter.png"),
                     QStringLiteral("bc"));
    mgr->registerCommand(factory.createCommand("draw.boxcenter", ctx), info,
                         "boxcenter");
  }


  {
    CommandInfo info(QStringLiteral("tab.draw.circlecenter"), tr("그리기"),
                     tr("원 중심"), QString(":/icons/ccenter.png"),
                     QStringLiteral("cc"));
    mgr->registerCommand(factory.createCommand("draw.circlecenter", ctx), info,
                         "circlecenter");
  }

  // {
  //   CommandInfo info(QStringLiteral("tab.draw.gridbubble"), tr("그리기"),
  //                    tr("주열번호"), QString(":/icons/bubble.png"),
  //                    QStringLiteral("gb"));
  //   mgr->registerCommand(factory.createCommand("draw.gridbubble", ctx), info,
  //                        "gridbubble");
  // }

  {
    CommandInfo info(QStringLiteral("tab.draw.surfacefinish"), tr("그리기"),
                     tr(" 거칠기 "), QString(":/icons/roughness.png"),
                     QStringLiteral("rg"));
    mgr->registerCommand(factory.createCommand("draw.surfacefinish", ctx), info,
                         "roughness");
  }

  {
    CommandInfo info(QStringLiteral("tab.draw.hole"), tr("그리기"),
                     tr("  구멍 R "), QString(":/icons/holeR.png"),
                     QStringLiteral("ho"));
    mgr->registerCommand(factory.createCommand("draw.hole", ctx), info,
                         "hole");
  }

  {
      CommandInfo info(QStringLiteral("tab.draw.holep"), tr("그리기"),
                       tr("  구멍 P "), QString(":/icons/holeP.png"),
                       QStringLiteral("hp"));
      mgr->registerCommand(factory.createCommand("draw.holep", ctx), info,
                           "holep");
  }

  {
    CommandInfo info(QStringLiteral("tab.draw.holesection"), tr("그리기"),
                     tr("구멍 단면"), QString(":/icons/holeD.png"),
                     QStringLiteral("hs"));
    mgr->registerCommand(factory.createCommand("draw.holesection", ctx), info,
                         "holesection");
  }


  // 탭바 modify
  // ----------------------------------------------------------------------------
  {
    CommandInfo info(QStringLiteral("tab.modify.move"), tr("편집하기"),
                     tr("  이동  "), QString(":/icons/moveItem.png"),
                     QStringLiteral("mo"));
    mgr->registerCommand(factory.createCommand("modify.move", ctx), info,
                         "move");
  }

  {
    CommandInfo info(QStringLiteral("tab.modify.copy"), tr("편집하기"),
                     tr("  복사  "), QString(":/icons/copyItem.png"),
                     QStringLiteral("co"));
    mgr->registerCommand(factory.createCommand("modify.copy", ctx), info,
                         "copy");
  }

  {
    CommandInfo info(QStringLiteral("tab.modify.mirror"), tr("편집하기"),
                     tr(" 거울상 "), QString(":/icons/mirror.png"),
                     QStringLiteral("mi"));
    mgr->registerCommand(factory.createCommand("modify.mirror", ctx), info,
                         "mirror");
  }

  {
    CommandInfo info(QStringLiteral("tab.modify.quadmirror"), tr("편집하기"),
                     tr("4거울상"), QString(":/icons/qmirror.png"),
                     QStringLiteral("qm"));
    mgr->registerCommand(factory.createCommand("modify.quadmirror", ctx),
                         info, "quadmirror");
  }

  {
    CommandInfo info(QStringLiteral("tab.modify.rotate"), tr("편집하기"),
                     tr("  회전  "), QString(":/icons/rotate.png"),
                     QStringLiteral("ro"));
    mgr->registerCommand(factory.createCommand("modify.rotate", ctx), info,
                         "rotate");
  }

  {
    CommandInfo info(QStringLiteral("tab.modify.scale"), tr("편집하기"),
                     tr("확대/축소"), QString(":/icons/scale.png"),
                     QStringLiteral("sc"));
    mgr->registerCommand(factory.createCommand("modify.scale", ctx), info,
                         "scale");
  }

  {
    CommandInfo info(QStringLiteral("tab.modify.erase"), tr("편집하기"),
                     tr(" 지우기 "), QString(":/icons/erase.png"),
                     QStringLiteral("er"));
    CommandBase *cmd = factory.createCommand("modify.erase", ctx);
    mgr->registerCommand(cmd, info, "erase");
  }

  {
    CommandInfo info(QStringLiteral("tab.modify.offset"), tr("편집하기"),
                     tr("  옵셋  "), QString(":/icons/offset.png"),
                     QStringLiteral("of"));
    CommandBase *cmd = factory.createCommand("modify.offset", ctx);
    mgr->registerCommand(cmd, info, "offset");
  }

  {
    CommandInfo info(QStringLiteral("tab.modify.trim"), tr("편집하기"),
                     tr(" 자르기 "), QString(":/icons/trim.png"),
                     QStringLiteral("tr"));
    mgr->registerCommand(factory.createCommand("modify.trim", ctx), info,
                         "trim");
  }

  {
    CommandInfo info(QStringLiteral("tab.modify.extend"), tr("편집하기"),
                     tr("  연장  "), QString(":/icons/extend.png"),
                     QStringLiteral("ex"));
    mgr->registerCommand(factory.createCommand("modify.extend", ctx), info,
                         "extend");
  }

  {
    CommandInfo info(QStringLiteral("tab.modify.fillet"), tr("편집하기"),
                     tr("둥근모따기"), QString(":/icons/fillet.png"),
                     QStringLiteral("fi"));
    mgr->registerCommand(factory.createCommand("modify.fillet", ctx), info,
                         "fillet");
  }

  {
      CommandInfo info(QStringLiteral("tab.modify.chamfer"), tr("편집하기"),
                       tr("직각모따기"), QString(":/icons/chamfer.png"),
                       QStringLiteral("ch"));
      mgr->registerCommand(factory.createCommand("modify.chamfer", ctx), info,
                           "chamfer");
  }

  {
    CommandInfo info(QStringLiteral("tab.modify.array"), tr("편집하기"),
                     tr("  배열하기  "), QString(":/icons/array.png"),
                     QStringLiteral("ar"));
    mgr->registerCommand(factory.createCommand("modify.array", ctx), info,
                         "array");
  }

  {
    CommandInfo info(QStringLiteral("tab.modify.stretch"), tr("편집하기"),
                     tr(" 늘리기 "), QString(":/icons/stretch.png"),
                     QStringLiteral("st"));
    mgr->registerCommand(factory.createCommand("modify.stretch", ctx), info,
                         "stretch");
  }

  {
    CommandInfo info(QStringLiteral("tab.modify.sever"), tr("편집하기"),
                     tr(" 끊기 "), QString(":/icons/sever.png"),
                     QStringLiteral("sv"));
    mgr->registerCommand(factory.createCommand("modify.sever", ctx), info,
                         "sever");
  }

  {
      CommandInfo info(QStringLiteral("tab.modify.join"), tr("편집하기"),
                       tr(" 연결하기 "), QString(":/icons/join.png"),
                       QStringLiteral("jo"));
      mgr->registerCommand(factory.createCommand("modify.join", ctx), info,
                           "join");
  }

  {
    CommandInfo info(QStringLiteral("tab.modify.explode"), tr("편집하기"), tr("분해하기"),
        QString(":/icons/explode.png"), // Using erase icon for now,
                                        // or assume an icon exists
        QStringLiteral("ex"));
    mgr->registerCommand(factory.createCommand("modify.explode", ctx), info,
                         "explode");
  }

  {
    CommandInfo info(
        QStringLiteral("tab.modify.overkill"), tr("편집하기"), tr("중복선제거"),
        QString(":/icons/overkill.png"), // Using erase icon for now,
        // or assume an icon exists
        QStringLiteral("ov"));
    mgr->registerCommand(factory.createCommand("modify.overkill", ctx), info,
                         "overkill");
  }

  {
    CommandInfo info(QStringLiteral("tab.modify.detailview"), tr("편집하기"),
                     tr(" 상세도 "), QString(":/icons/za.png"),
                     QStringLiteral("dv"));
    mgr->registerCommand(factory.createCommand("modify.detailview", ctx), info,
                         "detailview");
  }


  // 탭바 view
  // --------------------------------------------------------------------------------
  {
    CommandInfo info(QStringLiteral("tab.view.limits"), tr(" 보기 "),
                     tr("  도면틀  "), QString(":/icons/limits.png"),
                     QStringLiteral("li"));
    mgr->registerCommand(factory.createCommand("view.limits", ctx), info,
                         "limits");
  }

  {
    CommandInfo info(QStringLiteral("tab.view.zoomall"), tr(" 보기 "),
                     tr("  모두보기 "), QString(":/icons/za.png"),
                     QStringLiteral("za"));
    mgr->registerCommand(factory.createCommand("view.zoomall", ctx), info,
                         "zoomall");
  }

  {
    CommandInfo info(QStringLiteral("tab.view.zoomextend"), tr(" 보기 "),
                     tr("  맞게보기  "), QString(":/icons/ze.png"),
                     QStringLiteral("ze"));
    mgr->registerCommand(factory.createCommand("view.zoomextend", ctx), info,
                         "zooextend");
  }

  {
    CommandInfo info(QStringLiteral("tab.view.zoomwindow"), tr(" 보기 "),
                     tr(" 상자보기 "), QString(":/icons/zw.png"),
                     QStringLiteral("zw"));
    mgr->registerCommand(factory.createCommand("view.zoomwindow", ctx), info,
                         "zoomwindow");
  }

  // 탭바 text
  // ---------------------------------------------------------------------------------
  {
    CommandInfo info(QStringLiteral("tab.text.text"), tr(" 글자쓰기 "), tr("여러줄"),
                     QString(":/icons/text.png"), QStringLiteral("te"));
    mgr->registerCommand(factory.createCommand("text.text", ctx), info, "text");
  }

  {
    CommandInfo info(QStringLiteral("tab.text.dtext"), tr(" 글자쓰기 "), tr("한줄"),
                     QString(":/icons/dtext.png"),
                     QStringLiteral("dt")); // Using same icon for now
    mgr->registerCommand(factory.createCommand("text.dtext", ctx), info,
                         "dtext");
  }

  {
    CommandInfo info(QStringLiteral("tab.text.boxtext"), tr(" 글자쓰기 "), tr("박스글자"),
                     QString(":/icons/tbox.png"),
                     QStringLiteral("bt")); // Using boxdim icon for now
    mgr->registerCommand(factory.createCommand("text.boxtext", ctx), info,
                         "boxtext");
  }

  // 탭바 dimension
  // --------------------------------------------------------------------------
  {
      CommandInfo info(QStringLiteral("tab.dimension.style"), tr(" 치수선 "),
                       tr(" 스타일 "), QString(":/icons/dimstyle.png"),
                       QStringLiteral("dis"));
      mgr->registerCommand(factory.createCommand("draw.dimstyle", ctx), info,
                           "dimstyle");
  }

  {
      CommandInfo info(QStringLiteral("tab.dimension.linearDimHV"), tr(" 치수선 "),
                       tr("수평/수직"), QString(":/icons/dimlinear.png"),
                       QStringLiteral("dli"));
      mgr->registerCommand(factory.createCommand("draw.dimlinear_hv", ctx), info,
                           "dimlinear");
  }

  {
    CommandInfo info(QStringLiteral("tab.dimension.linearDim"), tr(" 치수선 "),
                     tr("  정렬치수 "), QString(":/icons/dimaligne.png"),
                     QStringLiteral("lid"));
    mgr->registerCommand(factory.createCommand("draw.linearDim", ctx), info,
                         "ldim");
  }

  {
    CommandInfo info(QStringLiteral("tab.dimension.radDim"), tr(" 치수선 "),
                     tr(" 원,호 치수 "), QString(":/icons/dimdiametric.png"),
                     QStringLiteral("dir"));
    mgr->registerCommand(factory.createCommand("draw.radDim", ctx), info,
                         "rdim");
  }

  {
    CommandInfo info(QStringLiteral("tab.dimension.angDim"), tr(" 치수선 "),
                     tr(" 각도(P) "), QString(":/icons/angpoint.png"),
                     QStringLiteral("dia"));
    mgr->registerCommand(factory.createCommand("draw.angDim", ctx), info,
                         "adim");
  }

  {
      CommandInfo info(QStringLiteral("tab.dimension.angular"), tr(" 치수선 "),
                       tr(" 각도(E) "), QString(":/icons/angent.png"),
                       QStringLiteral("dig"));
      mgr->registerCommand(factory.createCommand("draw.dimangular", ctx), info,
                           "gdim");
  }

  {
    CommandInfo info(QStringLiteral("tab.dimension.baseline"), tr(" 치수선 "),
                     tr(" 기준점 "), QString(":/icons/dimbas.png"),
                     QStringLiteral("dib"));
    mgr->registerCommand(factory.createCommand("draw.dimbaseline", ctx), info,
                         "bdim");
  }

  {
    CommandInfo info(QStringLiteral("tab.dimension.continue"), tr(" 치수선 "),
                     tr("연결치수"), QString(":/icons/dimcon.png"),
                     QStringLiteral("dic"));
    mgr->registerCommand(factory.createCommand("draw.dimcontinue", ctx), info,
                         "cdim");
  }

  {
    CommandInfo info(QStringLiteral("tab.dimension.autocontinuous"), tr(" 치수선 "),
                     tr("연속치수"), QString(":/icons/dimcon.png"),
                     QStringLiteral("dac"));
    mgr->registerCommand(factory.createCommand("draw.autocontinuousdim", ctx), info,
                         "acdim");
  }


  {
    CommandInfo info(QStringLiteral("tab.dimension.leader"), tr(" 치수선 "),
                     tr(" 지시선 "), QString(":/icons/dimleader.png"),
                     QStringLiteral("lea"));
    mgr->registerCommand(factory.createCommand("draw.leader", ctx), info,
                         "leader");
  }

  {
    CommandInfo info(QStringLiteral("tab.dimension.holecallout"), tr(" 치수선 "),
                     tr("구멍 콜아웃"), QString(":/icons/holeC.png"),
                     QStringLiteral("hc"));
    mgr->registerCommand(factory.createCommand("draw.holecallout", ctx), info,
                         "holecallout");
  }

  {
    CommandInfo info(QStringLiteral("tab.dimension.halfdim"), tr(" 치수선 "),
                     tr(" 대칭치수 "), QString(":/icons/dimhalf.png"),
                     QStringLiteral("dih"));
    mgr->registerCommand(factory.createCommand("draw.halfdim", ctx), info,
                         "hdim");
  }

  {
    CommandInfo info(QStringLiteral("tab.dimension.areadim"), tr(" 치수선 "),
                     tr(" 영역치수 "), QString(":/icons/boxdim.png"),
                     QStringLiteral("dar"));
    mgr->registerCommand(factory.createCommand("draw.areadim", ctx), info,
                         "areadim");
  }

  {
    CommandInfo info(QStringLiteral("tab.dimension.dimlea"), tr(" 치수선 "),
                     tr("치수지시선"), QString(":/icons/dimexp.png"),
                     QStringLiteral("dle"));
    mgr->registerCommand(factory.createCommand("draw.dimlea", ctx), info,
                         "dimlea");
  }

  {
      CommandInfo info(QStringLiteral("tab.dimension.dimmove"), tr(" 치수선 "),
                       tr("치수이동"), QString(":/icons/dimtxtmove.png"),
                       QStringLiteral("dm"));
      mgr->registerCommand(factory.createCommand("draw.dimMoveText", ctx), info,
                           "dimmove");
  }

  {
    CommandInfo info(QStringLiteral("tab.dimension.dimmerge"), tr(" 치수선 "),
                     tr("치수병합"), QString(":/icons/dimmerge.png"),
                     QStringLiteral("dmg"));
    mgr->registerCommand(factory.createCommand("draw.dimMerge", ctx), info,
                         "dimmerge");
  }

  {
    CommandInfo info(QStringLiteral("tab.dimension.dimsplit"), tr(" 치수선 "),
                     tr("치수분리"), QString(":/icons/dimsplit.png"),
                     QStringLiteral("dsp"));
    mgr->registerCommand(factory.createCommand("draw.dimSplit", ctx), info,
                         "dimsplit");
  }

  {
    CommandInfo info(QStringLiteral("tab.dimension.dimtextedit"),
                     tr(" 치수선 "), tr(" 치수편집 "),
                     QString(":/icons/dimtol.png"), QStringLiteral("dit"));
    mgr->registerCommand(factory.createCommand("draw.dimTextEdit", ctx), info,
                         "dimedit");
  }

  // [GDT 추가] Dimension 탭에 GDT 단축키 GT 등록
  {
    CommandInfo info(QStringLiteral("tab.dimension.gdt"), tr(" 치수선 "),
                     tr(" 기하공차 "), QString(":/icons/tol.png"),
                     QStringLiteral("gt"));
    mgr->registerCommand(factory.createCommand("draw.gdt", ctx), info, "gdt");
  }

  {
    CommandInfo info(QStringLiteral("tab.dimension.datum"), tr(" 치수선 "),
                     tr(" 데이텀 "), QString(":/icons/datum.png"),
                     QStringLiteral("dtu"));
    mgr->registerCommand(factory.createCommand("draw.datum", ctx), info,
                         "datum");
  }

  {
    CommandInfo info(QStringLiteral("tab.dimension.gdtedittool"),
                     tr(" 치수선 "), tr(" 기하편집 "),
                     QString(":/icons/toledit.png"), QStringLiteral("get"));
    mgr->registerCommand(factory.createCommand("draw.gdtedittool", ctx), info,
                         "gdtedit");
  }

  // 탭바 multiline (다중선)
  // --------------------------------------------------------------------------
  {
    CommandInfo info(QStringLiteral("tab.multiline.twoline"), tr(" 다중선 "),
                     tr(" 2-Line "), QString(":/icons/2line.png"),
                     QStringLiteral("2l"));
    mgr->registerCommand(factory.createCommand("multiline.twoline", ctx), info,
                         "2line");
  }

  // {
  //   CommandInfo info(QStringLiteral("tab.multiline.setup"), tr(" 다중선 "),
  //                    tr(" Setup "), QString(":/icons/limits.png"),
  //                    QStringLiteral("2ls"));
  //   mgr->registerCommand(factory.createCommand("multiline.twoline.setup", ctx), info,
  //                        "2linesetup");
  // }

  {
    CommandInfo info(QStringLiteral("tab.multiline.fourline"), tr(" 다중선 "),
                     tr(" 4-Line "), QString(":/icons/4line.png"),
                     QStringLiteral("4l"));
    mgr->registerCommand(factory.createCommand("multiline.fourline", ctx), info,
                         "4line");
  }

  // {
  //   CommandInfo info(QStringLiteral("tab.multiline.fourline.setup"), tr(" 다중선 "),
  //                    tr(" 4-Line Setup "), QString(":/icons/limits.png"),
  //                    QStringLiteral("4ls"));
  //   mgr->registerCommand(factory.createCommand("multiline.fourline.setup", ctx), info,
  //                        "4linesetup");
  // }

  {
    CommandInfo info(QStringLiteral("tab.multiline.sixline"), tr(" 다중선 "),
                     tr(" 6-Line "), QString(":/icons/6line.png"),
                     QStringLiteral("6l"));
    mgr->registerCommand(factory.createCommand("multiline.sixline", ctx), info,
                         "6line");
  }

  // {
  //   CommandInfo info(QStringLiteral("tab.multiline.sixline.setup"), tr(" 다중선 "),
  //                    tr(" 6-Line Setup "), QString(":/icons/limits.png"),
  //                    QStringLiteral("6ls"));
  //   mgr->registerCommand(factory.createCommand("multiline.sixline.setup", ctx), info,
  //                        "6linesetup");
  // }

  // {
  //   CommandInfo info(QStringLiteral("tab.multiline.lcorner"), tr(" 다중선 "),
  //                    tr(" L-코너 "), QString(":/icons/l-corner.png"),
  //                    QStringLiteral("lc"));
  //   mgr->registerCommand(factory.createCommand("multiline.lcorner", ctx), info,
  //                        "mlcorner");
  // }

  {
    CommandInfo info(QStringLiteral("tab.multiline.raycast_lcorner"), tr(" 다중선 "),
                     tr("L-코너(R) "), QString(":/icons/l-corner.png"),
                     QStringLiteral("rlc"));
    mgr->registerCommand(factory.createCommand("multiline.raycast_lcorner", ctx), info,
                         "mrlcorner");
  }

  // {
  //   CommandInfo info(QStringLiteral("tab.multiline.tcorner"), tr(" 다중선 "),
  //                    tr(" T-코너 "), QString(":/icons/t-corner.png"),
  //                    QStringLiteral("tc"));
  //   mgr->registerCommand(factory.createCommand("multiline.tcorner", ctx), info,
  //                        "mtcorner");
  // }

  {
    CommandInfo info(QStringLiteral("tab.multiline.raycast_tcorner"), tr(" 다중선 "),
                     tr("T-코너(R) "), QString(":/icons/t-corner.png"),
                     QStringLiteral("rtc"));
    mgr->registerCommand(factory.createCommand("multiline.raycast_tcorner", ctx), info,
                         "mrtcorner");
  }

  {
    CommandInfo info(QStringLiteral("tab.multiline.ccorner"), tr(" 다중선 "),
                     tr(" C-코너 "), QString(":/icons/c-corner.png"),
                     QStringLiteral("cc"));
    mgr->registerCommand(factory.createCommand("multiline.ccorner", ctx), info,
                         "mccorner");
  }

  // {
  //   CommandInfo info(QStringLiteral("tab.multiline.area_lcorner"), tr(" 다중선 "),
  //                    tr(" 영역 L-코너 "), QString(":/icons/l-corner.png"),
  //                    QStringLiteral("alc"));
  //   mgr->registerCommand(factory.createCommand("multiline.area_lcorner", ctx), info,
  //                        "mlarealcorner");
  // }

  // {
  //   CommandInfo info(QStringLiteral("tab.multiline.area_tcorner"), tr(" 다중선 "),
  //                    tr(" 영역 T-코너 "), QString(":/icons/t-corner.png"),
  //                    QStringLiteral("atc"));
  //   mgr->registerCommand(factory.createCommand("multiline.area_tcorner", ctx), info,
  //                        "mlareatcorner");
  // }

  // {
  //   CommandInfo info(QStringLiteral("tab.multiline.area_ccorner"), tr(" 다중선 "),
  //                    tr(" 영역 C-코너 "), QString(":/icons/c-corner.png"),
  //                    QStringLiteral("acc"));
  //   mgr->registerCommand(factory.createCommand("multiline.area_ccorner", ctx), info,
  //                        "mlareaccorner");
  // }

  {
    // 편심2-Line: 2-Line 아이콘 재사용, 단축키 el
    CommandInfo info(QStringLiteral("tab.multiline.eccentrictwoline"), tr(" 다중선 "),
                     tr(" 편심2-Line "), QString(":/icons/2line.png"),
                     QStringLiteral("el"));
    mgr->registerCommand(factory.createCommand("multiline.eccentrictwoline", ctx), info,
                         "eccentrictwoline");
  }

  {
    // 편심4-Line: 4-Line 아이콘 재사용, 단축키 e4l
    CommandInfo info(QStringLiteral("tab.multiline.eccentricfourline"), tr(" 다중선 "),
                     tr(" 편심4-Line "), QString(":/icons/4line.png"),
                     QStringLiteral("e4l"));
    mgr->registerCommand(factory.createCommand("multiline.eccentricfourline", ctx), info,
                         "eccentricfourline");
  }

  {
    // 편심6-Line: 6-Line 아이콘 재사용, 단축키 e6l
    CommandInfo info(QStringLiteral("tab.multiline.eccentricsixline"), tr(" 다중선 "),
                     tr(" 편심6-Line "), QString(":/icons/6line.png"),
                     QStringLiteral("e6l"));
    mgr->registerCommand(factory.createCommand("multiline.eccentricsixline", ctx), info,
                         "eccentricsixline");
  }

  factory.registerCommand("multiline.wallsettings", WallSettingsCommand::createCommand);
  {
    CommandInfo info(QStringLiteral("tab.multiline.wallsettings"), tr(" 다중선 "),
                     tr(" 벽체설정 "), QString(":/icons/layerSet.png"),
                     QStringLiteral("wset"));
    mgr->registerCommand(factory.createCommand("multiline.wallsettings", ctx), info,
                         "multilinewallsettings");
  }

  factory.registerCommand("multiline.wall", WallTypeCommand::createCommand);
  {
    CommandInfo info(QStringLiteral("tab.multiline.wall"), tr(" 다중선 "),
                     tr("단일  벽 "), QString(":/icons/s-wall.png"),
                     QStringLiteral("wl"));
    mgr->registerCommand(factory.createCommand("multiline.wall", ctx), info,
                         "multilinewall");
  }

  factory.registerCommand("multiline.wallmortar", WallTypeCommand::createMortarCommand);
  {
    CommandInfo info(QStringLiteral("tab.multiline.wallmortar"), tr(" 다중선 "),
                     tr(" 벽+몰탈 "), QString(":/icons/m-wall.png"),
                     QStringLiteral("wm"));
    mgr->registerCommand(factory.createCommand("multiline.wallmortar", ctx), info,
                         "multilinewallmortar");
  }

  factory.registerCommand("multiline.wallspacemortar", WallTypeCommand::createSpaceMortarCommand);
  {
    CommandInfo info(QStringLiteral("tab.multiline.wallspacemortar"), tr(" 다중선 "),
                     tr(" 벽+공간+몰탈 "), QString(":/icons/ms-wall.png"),
                     QStringLiteral("wsm"));
    mgr->registerCommand(factory.createCommand("multiline.wallspacemortar", ctx), info,
                         "multilinewallspacemortar");
  }

  // 탭바 건축유틸 (다중선 탭 바로 옆)
  // --------------------------------------------------------------------------
  {
      CommandInfo info(QStringLiteral("tab.draw.gridcenter"), tr("건축 Tools"),
                       tr("멀티중심선 "), QString(":/icons/mcenter.png"),
                       QStringLiteral("cl"));
      mgr->registerCommand(factory.createCommand("draw.gridcenter", ctx), info,
                           "mcenterline");
  }
  {
      CommandInfo info(QStringLiteral("tab.draw.gridbubble"), tr("건축 Tools"),
                       tr("주열번호"), QString(":/icons/bubble.png"),
                       QStringLiteral("gb"));
      mgr->registerCommand(factory.createCommand("draw.gridbubble", ctx), info,
                           "gridbubble");
  }
  factory.registerCommand("archutil.beamsign", BeamSignCommand::createCommand);
  {
      CommandInfo info(QStringLiteral("tab.archutil.beamsign"), tr("건축 Tools"),
                       tr(" 보 부호 "), QString(":/icons/beamsign.png"),
                       QStringLiteral("bsg"));
      mgr->registerCommand(factory.createCommand("archutil.beamsign", ctx), info,
                           "beamsign");
  }
  factory.registerCommand("archutil.beamschedule", BeamScheduleCommand::createCommand);
  {
      CommandInfo info(QStringLiteral("tab.archutil.beamschedule"), tr("건축 Tools"),
                       tr(" 보 일람표 "), QString(":/icons/beamlist.png"),
                       QStringLiteral("bsl"));
      mgr->registerCommand(factory.createCommand("archutil.beamschedule", ctx), info,
                           "beamschedule");
  }
  factory.registerCommand("archutil.column", ColumnCommand::createCommand);
  factory.registerCommand("archutil.elevator", ElevatorCommand::createCommand);
  {
      CommandInfo info(QStringLiteral("tab.archutil.elevator"), tr("건축 Tools"),
                       tr(" 엘리베이터 "), QString(":/icons/elev.png"),
                       QStringLiteral("elv"));
      mgr->registerCommand(factory.createCommand("archutil.elevator", ctx), info,
                           "elevator");
  }

  factory.registerCommand("archutil.stairplan", StairPlanCommand::createCommand);
  {
    CommandInfo info(QStringLiteral("tab.archutil.stairplan"), tr("건축 Tools"),
                     tr(" 계단평면 "), QString(":/icons/stair.png"),
                     QStringLiteral("stp"));
    mgr->registerCommand(factory.createCommand("archutil.stairplan", ctx), info,
                         "stairplan");
  }

  factory.registerCommand("archutil.rubble", RubbleCommand::createCommand);
  {
    CommandInfo info(QStringLiteral("tab.archutil.rubble"), tr("건축 Tools"),
                     tr(" 잡석깔기 "), QString(":/icons/rubble.png"),
                     QStringLiteral("jsg"));
    mgr->registerCommand(factory.createCommand("archutil.rubble", ctx), info,
                         "rubble");
  }


  factory.registerCommand("archutil.rib", RibCommand::createCommand);
  {
    CommandInfo info(QStringLiteral("tab.archutil.rib"), tr("건축 Tools"),
                     tr(" Rib "), QString(":/icons/rib.png"),
                     QStringLiteral("rib"));
    mgr->registerCommand(factory.createCommand("archutil.rib", ctx), info,
                         "rib");
  }

  factory.registerCommand("archutil.insulation", InsulationCommand::createCommand);
  {
    CommandInfo info(QStringLiteral("tab.archutil.insulation"), tr("건축 Tools"),
                     tr(" 단열재 "), QString(":/icons/insulation.png"),
                     QStringLiteral("ins"));
    mgr->registerCommand(factory.createCommand("archutil.insulation", ctx), info,
                         "insulation");
  }
  {
      CommandInfo info(QStringLiteral("tab.archutil.column"), tr("건축 Tools"),
                       tr(" 기둥 "), QString(":/icons/column1.png"),
                       QStringLiteral("col"));
      mgr->registerCommand(factory.createCommand("archutil.column", ctx), info,
                           "column");
  }

  // 탭메뉴 plandoor (평면도어) 등록 명령어 -----------------------------------
  // 람다 금지 규칙 적용 → PlanDoorTool::createCommand 정적 함수 사용
  factory.registerCommand("plandoor.layer", DoorLayerCommand::createCommand);
  factory.registerCommand("plandoor.type1", PlanDoorTool::createCommand);
  factory.registerCommand("plandoor.double", PlanDoorDoubleTool::createCommand);
  factory.registerCommand("plandoor.biswing", PlanDoorBiSwingTool::createCommand);
  factory.registerCommand("plandoor.doublebiswing", PlanDoorDoubleBiSwingTool::createCommand);
  factory.registerCommand("plandoor.revolving", PlanRevolvingDoorTool::createCommand);
  factory.registerCommand("plandoor.d113", PlanDoorTool::createCommand113);
  factory.registerCommand("plandoor.d114", PlanDoorDoubleTool::createCommand114);
  factory.registerCommand("plandoor.simple", PlanDoorSimpleTool::createCommand);
  factory.registerCommand("plandoor.simpledouble", PlanDoorSimpleTool::createDoubleCommand);
  factory.registerCommand("plandoor.simplebiswing", PlanDoorSimpleTool::createBiSwingCommand);
  factory.registerCommand("plandoor.simpledoublebiswing", PlanDoorSimpleTool::createDoubleBiSwingCommand);
  factory.registerCommand("planwindow.settings", WindowSettingsCommand::createCommand);
  factory.registerCommand("planwindow.type1", PlanWindowTool::createCommand);
  factory.registerCommand("planwindow.type3", PlanWindowTool::createCommandThreeSash);
  factory.registerCommand("planwindow.type4", PlanWindowTool::createCommandFourSash);
  factory.registerCommand("planwindow.top", PlanWindowTool::createCommandTopOnly);
  factory.registerCommand("planwindow.top3", PlanWindowTool::createCommandTopOnlyThreeSash);
  factory.registerCommand("planwindow.top4", PlanWindowTool::createCommandTopOnlyFourSash);
  factory.registerCommand("planwindow.toplink", PlanWindowTool::createCommandTopOnlyLinked);

  // 탭바 plandoor (평면도어) UI 등록
  // --------------------------------------------------------------------------
  {
    // 도어 레이어설정: 평면도어 탭 맨 앞. 전 도어 공용 레이어(문틀/나머지/Trim 제외) 관리.
    CommandInfo info(QStringLiteral("tab.plandoor.layer"),
                     tr(" 평면도어 "),
                     tr("도어 설정"),
                     QString(":/icons/layerSet.png"),
                     QStringLiteral("pdl"));         // 단축키 pdl
    mgr->registerCommand(
        factory.createCommand("plandoor.layer", ctx), info, "plandoorlayer");
  }

  {
    // 탭 이름 " 평면도어 ", 서브메뉴 "Type1"
    // 아이콘 없음 (추후 icons.qrc에 추가 가능)
    CommandInfo info(QStringLiteral("tab.plandoor.type1"),
                     tr(" 평면도어 "),
                     tr("외여닫이"),
                     QString(":/icons/sdoor.png"),                      // 아이콘 없음
                     QStringLiteral("pd1"));         // 단축키 pd1
    mgr->registerCommand(
        factory.createCommand("plandoor.type1", ctx), info, "plandoor1");
  }

  {
    CommandInfo info(QStringLiteral("tab.plandoor.double"),
                     tr(" 평면도어 "),
                     tr("쌍여닫이"),
                     QString(":/icons/ddoor.png"),
                     QStringLiteral("pd2"));         // 단축키 pd2
    mgr->registerCommand(
        factory.createCommand("plandoor.double", ctx), info, "plandoordouble");
  }

  {
    // 양방향 여닫이(자재문): 같은 " 평면도어 " 탭에 서브메뉴로 추가.
    // 전용 아이콘이 아직 없어 외여닫이 아이콘(sdoor.png)을 재사용한다.
    CommandInfo info(QStringLiteral("tab.plandoor.biswing"),
                     tr(" 평면도어 "),
                     tr("양방여닫이"),
                     QString(":/icons/ssdoor.png"),   // 아이콘 재사용 (전용 아이콘 없음)
                     QStringLiteral("pd3"));         // 단축키 pd3
    mgr->registerCommand(
        factory.createCommand("plandoor.biswing", ctx), info, "plandoorbiswing");
  }

  {
    // 양방쌍여닫이: 같은 " 평면도어 " 탭에 서브메뉴로 추가.
    // 전용 아이콘이 아직 없어 외여닫이 아이콘(sdoor.png)을 재사용한다.
    CommandInfo info(QStringLiteral("tab.plandoor.doublebiswing"),
                     tr(" 평면도어 "),
                     tr("양방쌍여닫이"),
                     QString(":/icons/sddoor.png"),   // 아이콘 재사용 (전용 아이콘 없음)
                     QStringLiteral("pd4"));         // 단축키 pd4
    mgr->registerCommand(
        factory.createCommand("plandoor.doublebiswing", ctx), info, "plandoordoublebiswing");
  }

  {
    // 회전문: 같은 " 평면도어 " 탭에 서브메뉴로 추가.
    // 전용 아이콘이 아직 없어 외여닫이 아이콘(sdoor.png)을 재사용한다.
    CommandInfo info(QStringLiteral("tab.plandoor.revolving"),
                     tr(" 평면도어 "),
                     tr("회전문"),
                     QString(":/icons/rdoor.png"),   // 아이콘 재사용 (전용 아이콘 없음)
                     QStringLiteral("pd5"));         // 단축키 pd5
    mgr->registerCommand(
        factory.createCommand("plandoor.revolving", ctx), info, "plandoorrevolving");
  }

  {
    // door-113.lsp 형상: 6점 잼브 + 30° 열린 문짝 + 호. 회전문 뒤에 배치.
    CommandInfo info(QStringLiteral("tab.plandoor.d113"),
                     tr(" 평면도어 "),
                     tr("기타-113"),
                     QString(":/icons/door113.png"),
                     QStringLiteral("pd113"));
    mgr->registerCommand(
        factory.createCommand("plandoor.d113", ctx), info, "plandoord113");
  }

  {
    // door-113 형상 쌍여닫이 (문-113 옆). 아이콘은 쌍여닫이 아이콘 재사용.
    CommandInfo info(QStringLiteral("tab.plandoor.d114"),
                     tr(" 평면도어 "),
                     tr("기타-114"),
                     QString(":/icons/door114.png"),
                     QStringLiteral("pd114"));
    mgr->registerCommand(
        factory.createCommand("plandoor.d114", ctx), info, "plandoord114");
  }

  {
    // 단순문: 문틀·프레임 없이 문짝선 + 스윙호만 그리는 문. 회전문 옆에 배치.
    // 전용 아이콘이 없어 외여닫이 아이콘(sdoor.png)을 재사용한다.
    CommandInfo info(QStringLiteral("tab.plandoor.simple"),
                     tr(" 평면도어 "),
                     tr("외여닫이"),
                     QString(":/icons/sNdoor.png"),
                     QStringLiteral("pd6"));         // 단축키 pd6
    mgr->registerCommand(
        factory.createCommand("plandoor.simple", ctx), info, "plandoorsimple");
  }

  {
    // 단순 쌍여닫이 / 양방여닫이 / 양방쌍여닫이: 문틀·프레임 없음 + 마감선. 아이콘은 각 원본 문 아이콘 재사용.
    CommandInfo info(QStringLiteral("tab.plandoor.simpledouble"),
                     tr(" 평면도어 "),
                     tr("쌍여닫이"),
                     QString(":/icons/dNdoor.png"),
                     QStringLiteral("pd7"));
    mgr->registerCommand(
        factory.createCommand("plandoor.simpledouble", ctx), info, "simpledouble");
  }
  {
    CommandInfo info(QStringLiteral("tab.plandoor.simplebiswing"),
                     tr(" 평면도어 "),
                     tr("양방여닫이"),
                     QString(":/icons/sNsdoor.png"),
                     QStringLiteral("pd8"));
    mgr->registerCommand(
        factory.createCommand("plandoor.simplebiswing", ctx), info, "simplebiswing");
  }
  {
    CommandInfo info(QStringLiteral("tab.plandoor.simpledoublebiswing"),
                     tr(" 평면도어 "),
                     tr("양방쌍여닫이"),
                     QString(":/icons/sNddoor.png"),
                     QStringLiteral("pd9"));
    mgr->registerCommand(
        factory.createCommand("plandoor.simpledoublebiswing", ctx), info, "simpledoublebiswing");
  }

  // 탭바 planwindow (평면 창문) UI 등록
  // --------------------------------------------------------------------------
  {
    // 창문설정: 평면창문 탭 맨 앞. 창 공용 레이어·상세치수(Win1~7) 관리.
    CommandInfo info(QStringLiteral("tab.planwindow.settings"),
                     tr(" 평면 창문 "),
                     tr("창문설정"),
                     QString(":/icons/layerSet.png"),
                     QStringLiteral("pws"));         // 단축키 pws
    mgr->registerCommand(
        factory.createCommand("planwindow.settings", ctx), info, "planwindowsettings");
  }
  {
    CommandInfo info(QStringLiteral("tab.planwindow.type1"),
                     tr(" 평면 창문 "),
                     tr("2미서기"),
                     QString(":/icons/s-win1-1.png"),
                     QStringLiteral("pw1"));         // 단축키 pw1
    mgr->registerCommand(
        factory.createCommand("planwindow.type1", ctx), info, "planwindow1");
  }
  {
    CommandInfo info(QStringLiteral("tab.planwindow.type3"),
                     tr(" 평면 창문 "),
                     tr("3미서기"),
                     QString(":/icons/s-win3-1.png"),  // TODO: 전용 아이콘 필요
                     QStringLiteral("pw3"));         // 단축키 pw3
    mgr->registerCommand(
        factory.createCommand("planwindow.type3", ctx), info, "planwindow3");
  }
  {
    CommandInfo info(QStringLiteral("tab.planwindow.type4"),
                     tr(" 평면 창문 "),
                     tr("4미서기"),
                     QString(":/icons/s-win4-1.png"),  // TODO: 전용 아이콘 필요
                     QStringLiteral("pw4"));         // 단축키 pw4
    mgr->registerCommand(
        factory.createCommand("planwindow.type4", ctx), info, "planwindow4");
  }
  {
    CommandInfo info(QStringLiteral("tab.planwindow.top"),
                     tr(" 평면 창문 "),
                     tr("2상미서기"),
                     QString(":/icons/s-win1.png"),  // TODO: 전용 아이콘 필요
                     QStringLiteral("pwt"));         // 단축키 pwt
    mgr->registerCommand(
        factory.createCommand("planwindow.top", ctx), info, "planwindowtop");
  }
  {
    CommandInfo info(QStringLiteral("tab.planwindow.top3"),
                     tr(" 평면 창문 "),
                     tr("3상미서기"),
                     QString(":/icons/s-win3.png"),  // TODO: 전용 아이콘 필요
                     QStringLiteral("pwt3"));        // 단축키 pwt3
    mgr->registerCommand(
        factory.createCommand("planwindow.top3", ctx), info, "planwindowtop3");
  }
  {
    CommandInfo info(QStringLiteral("tab.planwindow.top4"),
                     tr(" 평면 창문 "),
                     tr("4상미서기"),
                     QString(":/icons/s-win4.png"),  // TODO: 전용 아이콘 필요
                     QStringLiteral("pwt4"));        // 단축키 pwt4
    mgr->registerCommand(
        factory.createCommand("planwindow.top4", ctx), info, "planwindowtop4");
  }
  {
    CommandInfo info(QStringLiteral("tab.planwindow.toplink"),
                     tr(" 평면 창문 "),
                     tr("전면창"),
                     QString(":/icons/s-win5.png"),  // TODO: 전용 아이콘 필요
                     QStringLiteral("pwtl"));        // 단축키 pwtl
    mgr->registerCommand(
        factory.createCommand("planwindow.toplink", ctx), info, "planwindowtoplink");
  }

  // ETC
  // -----------------------------------------------------------------------------------------
  {
    CommandInfo info(QStringLiteral("edit.undo"), tr("Edit"), tr("Undo"),
                     QString(":/icons/Undo1.png"), QStringLiteral("Ctrl+Z"));
    CommandBase *cmd = factory.createCommand("edit.undo", ctx);
    mgr->registerCommand(cmd, info, "undo");
  }

  {
    CommandInfo info(QStringLiteral("edit.redo"), tr("Edit"), tr("Redo"),
                     QString(":/icons/Redo1.png"), QStringLiteral("Ctrl+Y"));
    CommandBase *cmd = factory.createCommand("edit.redo", ctx);
    mgr->registerCommand(cmd, info, "redo");
  }

  {
    CommandInfo info(QStringLiteral("tab.block.wblock"), tr(" 블록 "),
                     tr(" 만들기 "), QString(":/icons/wblock.png"),
                     QStringLiteral("WB"));
    mgr->registerCommand(factory.createCommand("block.wblock", ctx), info,
                         "wblock");
  }

  {
    CommandInfo info(QStringLiteral("tab.block.insert"), tr(" 블록 "),
                     tr("불러오기"), QString(":/icons/insert.png"),
                     QStringLiteral("In"));
    mgr->registerCommand(factory.createCommand("block.insert", ctx), info,
                         "insert");
  }

  {
    CommandInfo info(QStringLiteral("tab.block.lib.furniture"), tr(" 블록 "),
                     tr("가구"), QString(":/icons/funi.png"), QString());
    mgr->registerCommand(factory.createCommand("block.lib.furniture", ctx), info,
                         QString());
  }

  {
    CommandInfo info(QStringLiteral("tab.block.lib.sink"), tr(" 블록 "),
                     tr("싱크대"), QString(":/icons/sink.png"), QString());
    mgr->registerCommand(factory.createCommand("block.lib.sink", ctx), info,
                         QString());
  }

  {
    CommandInfo info(QStringLiteral("tab.block.lib.toilet"), tr(" 블록 "),
                     tr("화장실"), QString(":/icons/tot.png"), QString());
    mgr->registerCommand(factory.createCommand("block.lib.toilet", ctx), info,
                         QString());
  }

  {
    CommandInfo info(QStringLiteral("tab.block.lib.tree"), tr(" 블록 "),
                     tr("나무"), QString(":/icons/tree.png"), QString());
    mgr->registerCommand(factory.createCommand("block.lib.tree", ctx), info,
                         QString());
  }
  {
  CommandInfo info(QStringLiteral("tab.block.lib.elev"), tr(" 블록 "),
                   tr("엘리베이터"), QString(":/icons/elev.png"), QString());
  mgr->registerCommand(factory.createCommand("block.lib.elev", ctx), info,
                       QString());
}


  // 탭바 기계부품
  // ---------------------------------------------------------------------------------------
  // 서브메뉴 뼈대만 먼저 등록. 아이콘/실제 그리기 기능은 항목별로 순차 추가 예정
  // (볼트 → 너트 → 와셔 → ... 순서, [[project_mechparts_plan]] 참고)
  {
    CommandInfo info(QStringLiteral("tab.mech.bolt"), tr("기계부품"),
                     tr("  볼트  "), QString(":/icons/bolt.png"), QString());
    mgr->registerCommand(factory.createCommand("mech.bolt", ctx), info,
                         QString());
  }

  {
    // 렌치볼트(육각구멍붙이볼트) — Lisp\wbolt.lsp 이식. "볼트" 탭 버튼 하나에서
    // BoltPickerDialog로 4종을 아이콘 선택하는 방식으로 통합됐으므로, 이 3개
    // (렌치볼트/접시머리렌치볼트/둥근머리렌치볼트)는 더 이상 별도 탭 버튼을 만들지
    // 않는다 — CommandInfo id에서 "tab." 접두어만 뺐다(mgr->registerCommand가
    // "tab."/"menu."로 시작하는 id일 때만 탭 버튼을 만듦, commandmanager.cpp 참고).
    // 타이핑 명령어("mech.wrenchbolt")와 별칭("wbolt")은 그대로 살아있다.
    CommandInfo info(QStringLiteral("mech.wrenchbolt"), tr("기계부품"),
                     tr("렌치볼트"), QString(), QStringLiteral("wbolt"));
    mgr->registerCommand(factory.createCommand("mech.wrenchbolt", ctx), info,
                         QStringLiteral("wbolt"));
  }

  {
    // 접시머리렌치볼트(육각구멍붙이 접시머리볼트) — Lisp\hhbolt.lsp 이식. 위와 동일하게
    // 탭 버튼 없이 타이핑 명령어("mech.hhbolt"/"hhbolt")만 남겨둠.
    CommandInfo info(QStringLiteral("mech.hhbolt"), tr("기계부품"),
                     tr("접시머리렌치볼트"), QString(), QStringLiteral("hhbolt"));
    mgr->registerCommand(factory.createCommand("mech.hhbolt", ctx), info,
                         QStringLiteral("hhbolt"));
  }

  {
    // 둥근머리렌치볼트(육각구멍붙이 둥근머리볼트) — Lisp\hb.lsp 이식. 위와 동일.
    CommandInfo info(QStringLiteral("mech.hbbolt"), tr("기계부품"),
                     tr("둥근머리렌치볼트"), QString(), QStringLiteral("hbbolt"));
    mgr->registerCommand(factory.createCommand("mech.hbbolt", ctx), info,
                         QStringLiteral("hbbolt"));
  }

  {
    CommandInfo info(QStringLiteral("tab.mech.nut"), tr("기계부품"),
                     tr("  너트  "), QString(), QString());
    mgr->registerCommand(factory.createCommand("mech.nut", ctx), info,
                         QString());
  }

  {
    CommandInfo info(QStringLiteral("tab.mech.washer"), tr("기계부품"),
                     tr("  와셔  "), QString(), QString());
    mgr->registerCommand(factory.createCommand("mech.washer", ctx), info,
                         QString());
  }

  {
    CommandInfo info(QStringLiteral("tab.mech.key"), tr("기계부품"),
                     tr("   키   "), QString(), QString());
    mgr->registerCommand(factory.createCommand("mech.key", ctx), info,
                         QString());
  }

  {
    CommandInfo info(QStringLiteral("tab.mech.pin"), tr("기계부품"),
                     tr("   핀   "), QString(), QString());
    mgr->registerCommand(factory.createCommand("mech.pin", ctx), info,
                         QString());
  }

  {
    CommandInfo info(QStringLiteral("tab.mech.bearing"), tr("기계부품"),
                     tr(" 베어링 "), QString(), QString());
    mgr->registerCommand(factory.createCommand("mech.bearing", ctx), info,
                         QString());
  }

  {
    CommandInfo info(QStringLiteral("tab.mech.spring"), tr("기계부품"),
                     tr(" 스프링 "), QString(), QString());
    mgr->registerCommand(factory.createCommand("mech.spring", ctx), info,
                         QString());
  }

  {
    CommandInfo info(QStringLiteral("tab.mech.retainingring"), tr("기계부품"),
                     tr("리테이닝링"), QString(), QString());
    mgr->registerCommand(factory.createCommand("mech.retainingring", ctx), info,
                         QString());
  }

  {
    CommandInfo info(QStringLiteral("tab.mech.structuralsteel"), tr("기계부품"),
                     tr("구조형강"), QString(), QString());
    mgr->registerCommand(factory.createCommand("mech.structuralsteel", ctx), info,
                         QString());
  }

//   {
//     CommandInfo info(QStringLiteral("view.limits"), tr("View"),
//                      tr("Drawing Limits"), QString(":/icons/settings.png"),
//                      QStringLiteral("Lis"));
//     mgr->registerCommand(factory.createCommand("view.limits", ctx), info,
//                          "limits");
//   }
 }
