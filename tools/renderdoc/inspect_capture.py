"""Run in RenderDoc 1.46's Python Scripting pane with a saved capture open.

Writes local JSON and optional texture previews beside the capture, under
capture-analysis/<capture stem>. Keep captures and exports in ignored out/.
Uses only replay APIs; it does not modify the captured game or upload anything.
"""

import json
import math
from pathlib import Path
import traceback

import renderdoc as rd

EXPORT_FLOAT_TEXTURES = True
DUMP_DRAW_BINDINGS = True


def json_value(obj, depth=0):
    if obj is None or isinstance(obj, (bool, int, float, str)):
        return obj
    if isinstance(obj, rd.ResourceId) or depth > 4:
        return str(obj)
    try:
        return [json_value(item, depth + 1) for item in obj]
    except TypeError:
        pass
    fields = {}
    for name in dir(obj):
        if name.startswith('_') or name in ('this', 'thisown'):
            continue
        value = getattr(obj, name)
        if not callable(value):
            fields[name] = json_value(value, depth + 1)
    return fields or str(obj)


def inspect_capture(controller, output):
    output.mkdir(parents=True, exist_ok=True)

    def save(name, value):
        (output / name).write_text(json.dumps(value, indent=2), encoding='utf-8')

    try:
        save('status.json', {'complete': False, 'stage': 'inventory'})
        resources = {str(r.resourceId): r.name for r in controller.GetResources()}
        save('resources.json', resources)
        textures = controller.GetTextures()
        save('textures.json', [dict(
            id=str(t.resourceId), name=resources.get(str(t.resourceId)),
            width=t.width, height=t.height, depth=t.depth, arraySize=t.arraysize,
            mips=t.mips, format=t.format.Name(), samples=t.msSamp,
            flags=str(t.creationFlags)) for t in textures])
        save('buffers.json', [dict(id=str(b.resourceId), length=b.length,
                                  name=resources.get(str(b.resourceId)),
                                  flags=str(b.creationFlags))
                              for b in controller.GetBuffers()])
        structured = controller.GetStructuredFile()
        actions = []
        draw_events = []

        def walk(nodes):
            for a in nodes:
                actions.append(dict(eid=a.eventId, name=a.GetName(structured),
                    flags=str(a.flags), outputs=[str(r) for r in a.outputs],
                    depthOut=str(a.depthOut), indices=a.numIndices))
                if a.flags & rd.ActionFlags.Drawcall:
                    draw_events.append(a.eventId)
                walk(a.children)

        walk(controller.GetRootActions())
        save('actions.json', actions)
        summary = dict(actions=len(actions), draws=len(draw_events),
                       textures=len(textures),
                       ssxMarkers=sum('SSX ' in a['name'] for a in actions))
        save('summary.json', summary)

        if DUMP_DRAW_BINDINGS:
            draws = []
            for index, eid in enumerate(draw_events):
                controller.SetFrameEvent(eid, True)
                pipe = controller.GetPipelineState()
                pso = str(pipe.GetGraphicsPipelineObject())
                draws.append(dict(eid=eid, pso=pso, name=resources.get(pso),
                    viewport=json_value(pipe.GetViewport(0)),
                    vs=str(pipe.GetShader(rd.ShaderStage.Vertex)),
                    ps=str(pipe.GetShader(rd.ShaderStage.Pixel)),
                    reads=json_value(pipe.GetReadOnlyResources(rd.ShaderStage.Pixel)),
                    writes=json_value(pipe.GetReadWriteResources(rd.ShaderStage.Pixel))))
                if (index + 1) % 100 == 0:
                    save('status.json', {'complete': False, 'stage': 'draw bindings',
                                        'processed': index + 1, 'total': len(draw_events)})
            save('draws.json', draws)

        if EXPORT_FLOAT_TEXTURES and actions:
            save('status.json', {'complete': False, 'stage': 'texture snapshots'})
            # Synthetic grouping markers may have IDs above the last real event.
            # The frame's last action, rather than max(eventId), is the endpoint.
            controller.SetFrameEvent(actions[-1]['eid'], True)
            snapshots = []
            for t in textures:
                if 'FLOAT' not in t.format.Name():
                    continue
                mn, mx = controller.GetMinMax(t.resourceId, rd.Subresource(),
                                              rd.CompType.Typeless)
                row = dict(id=str(t.resourceId), width=t.width, height=t.height,
                           format=t.format.Name(), minimum=list(mn.floatValue),
                           maximum=list(mx.floatValue),
                           usage=json_value(controller.GetUsage(t.resourceId)))
                stem = str(t.resourceId).replace('ResourceId::', 'texture-')
                settings = rd.TextureSave()
                settings.resourceId = t.resourceId
                settings.mip = 0
                settings.destType = rd.FileType.EXR
                row['exrResult'] = str(controller.SaveTexture(settings,
                                             str(output / (stem + '.exr'))))
                # Preview remapping only. EXR and min/max preserve raw values;
                # this is not the game's tone mapping or an HDR output proof.
                peak = max(row['maximum'][:t.format.compCount if t.format.compCount == 1 else 3])
                preview_white = peak if math.isfinite(peak) and peak > 0 else 1.0
                settings.destType = rd.FileType.PNG
                settings.comp.blackPoint = 0.0
                settings.comp.whitePoint = preview_white
                if t.format.compCount == 1:
                    settings.channelExtract = 0
                row['previewWhite'] = preview_white
                row['pngResult'] = str(controller.SaveTexture(settings,
                                             str(output / (stem + '.png'))))
                snapshots.append(row)
            save('float-textures.json', snapshots)
        save('status.json', {'complete': True})
    except Exception:
        save('status.json', {'complete': False, 'error': traceback.format_exc()})


if 'pyrenderdoc' in globals():
    capture = Path(pyrenderdoc.GetCaptureFilename())
    if capture.suffix.lower() != '.rdc':
        raise RuntimeError('Open and save a .rdc capture before running this script.')
    destination = capture.parent / 'capture-analysis' / capture.stem
    previous_event = pyrenderdoc.CurEvent()

    def replay(controller):
        try:
            inspect_capture(controller, destination)
        finally:
            controller.SetFrameEvent(previous_event, True)

    pyrenderdoc.Replay().BlockInvoke(replay)
    print('Capture analysis: ' + str(destination / 'status.json'))
