"""Export the native SSX motion pass from a local RenderDoc capture.

Run in RenderDoc's Python Scripting pane after saving the capture. Exports stay
beside the .rdc under capture-analysis; they contain game-derived data and must
remain outside version control. Does not change the capture or game state.
"""
import json
import traceback
from pathlib import Path
import renderdoc as rd


def inspect_native_motion(controller, destination):
    destination.mkdir(parents=True, exist_ok=True)
    def save(name, value):
        (destination / name).write_text(json.dumps(value, indent=2), encoding='utf-8')
    def variable(v):
        count = v.rows * v.columns
        return dict(name=v.name, rows=v.rows, columns=v.columns,
                    floats=list(v.value.f32v)[:count], uints=list(v.value.u32v)[:count],
                    members=[variable(m) for m in v.members])
    try:
        names = {r.resourceId: r.name for r in controller.GetResources()}
        textures = {t.resourceId: t for t in controller.GetTextures()}
        native_pipelines = {r for r, name in names.items() if 'SSX experimental native rider motion' in name}
        actions = []
        def walk(nodes):
            for node in nodes:
                if node.flags & rd.ActionFlags.Drawcall:
                    actions.append(node)
                walk(node.children)
        walk(controller.GetRootActions())
        draws = []
        for action in actions:
            controller.SetFrameEvent(action.eventId, True)
            pipe = controller.GetPipelineState()
            pipeline = pipe.GetGraphicsPipelineObject()
            if pipeline not in native_pipelines:
                continue
            save('status.json', dict(complete=False, event=action.eventId, native_draws=len(draws)+1))
            row = dict(event=action.eventId, count=action.numIndices, constants=[], reads=[])
            reflection = pipe.GetShaderReflection(rd.ShaderStage.Vertex)
            for i, block in enumerate(reflection.constantBlocks):
                bound = pipe.GetConstantBlock(rd.ShaderStage.Vertex, i, 0).descriptor
                values = controller.GetCBufferVariableContents(pipeline,
                    pipe.GetShader(rd.ShaderStage.Vertex), rd.ShaderStage.Vertex,
                    pipe.GetShaderEntryPoint(rd.ShaderStage.Vertex), i,
                    bound.resource, bound.byteOffset, bound.byteSize)
                row['constants'].append(dict(name=block.name, variables=[variable(v) for v in values]))
            for binding in pipe.GetReadOnlyResources(rd.ShaderStage.Vertex):
                d = binding.descriptor
                row['reads'].append(dict(resource=str(d.resource), name=names.get(d.resource),
                                          offset=d.byteOffset, size=d.byteSize))
            draws.append(row)
        save('draws.json', draws)
        if not draws:
            save('status.json', dict(complete=False, reason='No native motion draws in this capture.'))
            return
        controller.SetFrameEvent(draws[-1]['event'], True)
        pipe = controller.GetPipelineState()
        outputs = [b.resource for b in pipe.GetOutputTargets()][:2]
        depth = [r.descriptor.resource for r in pipe.GetReadOnlyResources(rd.ShaderStage.Pixel)
                 if r.descriptor.resource in textures]
        chosen = [('motion', outputs[0]), ('validity', outputs[1])]
        if depth:
            chosen.append(('depth', depth[0]))
        # Any color snapshots included in the capture are exported with IDs;
        # do not silently assume a particular one is the matching scene frame.
        chosen += [('scene-'+str(r).split('::')[-1], r) for r, name in names.items()
                   if 'SSX encoded scene color snapshot' in name and r in textures]
        exported = []
        for label, resource in chosen:
            t = textures[resource]
            raw = bytes(controller.GetTextureData(resource, rd.Subresource(0, 0, 0)))
            (destination / (label+'.bin')).write_bytes(raw)
            exported.append(dict(label=label, resource=str(resource), name=names.get(resource),
                                 width=t.width, height=t.height, format=t.format.Name(), bytes=len(raw)))
        # Retain both immutable geometry snapshots for identity/pose inspection.
        for i, binding in enumerate(pipe.GetReadOnlyResources(rd.ShaderStage.Vertex)):
            d = binding.descriptor
            raw = bytes(controller.GetBufferData(d.resource, 0, 8*1024*1024))
            (destination / ('geometry-%d.bin' % i)).write_bytes(raw)
        save('textures.json', exported)
        save('status.json', dict(complete=True, native_draws=len(draws), event=draws[-1]['event']))
    except Exception:
        save('status.json', dict(complete=False, error=traceback.format_exc()))


if 'pyrenderdoc' in globals():
    capture = Path(pyrenderdoc.GetCaptureFilename())
    if capture.suffix.lower() != '.rdc':
        raise RuntimeError('Save the capture to a local .rdc before analysis.')
    previous = pyrenderdoc.CurEvent()
    destination = capture.parent / 'capture-analysis' / capture.stem / 'native-motion'
    def replay_native_motion(controller):
        try:
            inspect_native_motion(controller, destination)
        finally:
            controller.SetFrameEvent(previous, True)
    pyrenderdoc.Replay().BlockInvoke(replay_native_motion)
    print('Native motion analysis: ' + str(destination / 'status.json'))
