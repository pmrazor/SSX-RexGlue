"""Dump selected draw constants/disassembly in RenderDoc 1.46, locally only.

Open a saved capture and select a draw, then run this in Python Scripting.
Set EVENTS to explicit draw IDs for a batch; the default uses the selected event.
Guest float constants may be packed: match the disassembly before treating array
indices as original Xenos register numbers. This script does not infer matrices.
"""

import json
from pathlib import Path
import traceback

import renderdoc as rd

EVENTS = ()


def shader_variable(value):
    count = value.rows * value.columns
    result = dict(name=value.name, type=str(value.type), rows=value.rows,
                  columns=value.columns)
    # Preserve both interpretations for packed guest constants, including flags.
    if count:
        result['floats'] = list(value.value.f32v)[:count]
        result['uints'] = list(value.value.u32v)[:count]
    if value.members:
        result['members'] = [shader_variable(v) for v in value.members]
    return result


def inspect_passes(controller, output, events):
    output.mkdir(parents=True, exist_ok=True)

    def save(name, value):
        (output / name).write_text(json.dumps(value, indent=2), encoding='utf-8')

    try:
        resources = {r.resourceId: r.name for r in controller.GetResources()}
        textures = {t.resourceId: t for t in controller.GetTextures()}
        for eid in events:
            save('status.json', dict(complete=False, event=eid))
            controller.SetFrameEvent(eid, True)
            pipe = controller.GetPipelineState()
            pipeline = pipe.GetGraphicsPipelineObject()
            viewport = pipe.GetViewport(0)
            row = dict(eid=eid, pipeline=str(pipeline), name=resources.get(pipeline),
                       viewport=dict(x=viewport.x, y=viewport.y,
                                     width=viewport.width, height=viewport.height),
                       stages={})
            for stage in (rd.ShaderStage.Vertex, rd.ShaderStage.Pixel):
                reflection = pipe.GetShaderReflection(stage)
                if not reflection:
                    continue
                key = str(stage).split('.')[-1]
                (output / ('%d-%s.txt' % (eid, key))).write_text(
                    controller.DisassembleShader(pipeline, reflection, ''), encoding='utf-8')
                blocks = []
                for index, block in enumerate(reflection.constantBlocks):
                    bound = pipe.GetConstantBlock(stage, index, 0).descriptor
                    values = controller.GetCBufferVariableContents(
                        pipeline, pipe.GetShader(stage), stage,
                        pipe.GetShaderEntryPoint(stage), index,
                        bound.resource, bound.byteOffset, bound.byteSize)
                    blocks.append(dict(name=block.name, resource=str(bound.resource),
                                       offset=bound.byteOffset, size=bound.byteSize,
                                       variables=[shader_variable(v) for v in values]))
                reads = []
                for resource in pipe.GetReadOnlyResources(stage):
                    bound = resource.descriptor
                    texture = textures.get(bound.resource)
                    item = dict(resource=str(bound.resource), format=bound.format.Name(),
                                firstMip=bound.firstMip, numMips=bound.numMips,
                                firstSlice=bound.firstSlice, numSlices=bound.numSlices,
                                descriptorIndex=resource.access.arrayElement)
                    if texture:
                        item.update(width=texture.width, height=texture.height)
                        # Tiny terminal mip values are useful for exposure inputs.
                        # Interpret typeless resources through this draw's SRV format.
                        mip = bound.firstMip + bound.numMips - 1
                        if (bound.numMips > 1 and texture.width >> mip <= 1 and
                                texture.height >> mip <= 1 and
                                bound.format.compType in (rd.CompType.Float, rd.CompType.UNorm)):
                            low, high = controller.GetMinMax(bound.resource,
                                rd.Subresource(mip, bound.firstSlice, 0), bound.format.compType)
                            item['lastMip'] = dict(mip=mip,
                                minimum=list(low.floatValue), maximum=list(high.floatValue))
                    reads.append(item)
                row['stages'][key] = dict(constants=blocks, reads=reads)
            save('%d.json' % eid, row)
        save('status.json', dict(complete=True, events=list(events)))
    except Exception:
        save('status.json', dict(complete=False, error=traceback.format_exc()))


if 'pyrenderdoc' in globals():
    capture = Path(pyrenderdoc.GetCaptureFilename())
    if capture.suffix.lower() != '.rdc':
        raise RuntimeError('Open and save a .rdc capture before running this script.')
    previous = pyrenderdoc.CurEvent()
    destination = capture.parent / 'capture-analysis' / capture.stem / 'passes'

    def replay(controller):
        try:
            inspect_passes(controller, destination, EVENTS or (previous,))
        finally:
            controller.SetFrameEvent(previous, True)

    pyrenderdoc.Replay().BlockInvoke(replay)
    print('Pass analysis: ' + str(destination / 'status.json'))
