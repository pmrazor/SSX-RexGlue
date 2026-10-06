"""Read-only, local RenderDoc inspection of candidate SSX rider draws."""
import json
import math
import struct
import traceback
from pathlib import Path
import renderdoc as rd

EVENTS = () # Empty: inspect the selected draw; set explicit event IDs for a batch.


def inspect_native(controller, destination):
    destination.mkdir(parents=True, exist_ok=True)
    def save(name, value):
        (destination / name).write_text(json.dumps(value, indent=2), encoding='utf-8')
    try:
        results = []
        for eid in (EVENTS or (previous,)):
            save('status.json', dict(complete=False, event=eid))
            controller.SetFrameEvent(eid, True)
            pipe = controller.GetPipelineState()
            mesh = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
            fields = {}
            for name in dir(mesh):
                if name.startswith('_'):
                    continue
                value = getattr(mesh, name)
                if not callable(value):
                    fields[name] = value if isinstance(value, (int, float, bool, str)) else str(value)
            row = dict(eid=eid, mesh=fields, constants=[], signatures=[])
            refl = pipe.GetShaderReflection(rd.ShaderStage.Vertex)
            row['signatures'] = [dict(name=s.varName, semantic=s.semanticName,
                                      system=str(s.systemValue), count=s.compCount,
                                      register=s.regIndex) for s in refl.outputSignature]
            for i, block in enumerate(refl.constantBlocks):
                bound = pipe.GetConstantBlock(rd.ShaderStage.Vertex, i, 0).descriptor
                raw = bytes(controller.GetBufferData(bound.resource, bound.byteOffset, block.byteSize))
                (destination / ('%d-cb%d.bin' % (eid, i))).write_bytes(raw)
                row['constants'].append(dict(name=block.name, index=i,
                    size=len(raw), offset=bound.byteOffset, resource=str(bound.resource)))
                if block.name == 'xe_fetch_cbuffer':
                    fetch = struct.unpack_from('<2I', raw, 95 * 8)
                    row['fetch'] = dict(words=list(fetch), address=fetch[0] & ~3,
                        size=((fetch[1] >> 2) & 0xFFFFFF) * 4, endian=fetch[1] & 3)
            if 'fetch' in row:
                reads = pipe.GetReadOnlyResources(rd.ShaderStage.Vertex)
                row['reads'] = [dict(resource=str(r.descriptor.resource),
                                    offset=r.descriptor.byteOffset, size=r.descriptor.byteSize,
                                    index=r.access.index, element=r.access.arrayElement) for r in reads]
                # These shaders use only t0 for the guest shared-memory buffer.
                if len(reads) == 1 and row['fetch']['size'] <= 2 * 1024 * 1024:
                    raw = bytes(controller.GetBufferData(reads[0].descriptor.resource,
                                row['fetch']['address'], row['fetch']['size']))
                    (destination / ('%d-guest-vertices.bin' % eid)).write_bytes(raw)
            if mesh.indexResourceId != rd.ResourceId.Null():
                raw = bytes(controller.GetBufferData(mesh.indexResourceId, mesh.indexByteOffset,
                            mesh.indexByteSize))
                (destination / ('%d-postvs-indices.bin' % eid)).write_bytes(raw)
            if mesh.vertexResourceId != rd.ResourceId.Null() and mesh.vertexByteStride:
                # RenderDoc puts SV_Position first in post-VS data. Record the
                # exposed format and unproject flag so this remains auditable.
                raw = bytes(controller.GetBufferData(mesh.vertexResourceId, mesh.vertexByteOffset,
                            min(16 * 1024 * 1024, mesh.vertexByteSize)))
                (destination / ('%d-postvs.bin' % eid)).write_bytes(raw)
                positions = [struct.unpack_from('<4f', raw, o) for o in
                    range(0, len(raw) - 15, mesh.vertexByteStride)]
                ndc = [(p[0]/p[3], p[1]/p[3], p[2]/p[3], p[3]) for p in positions
                       if all(math.isfinite(v) for v in p) and p[3] > 1e-7]
                row['vertices'] = len(positions)
                if ndc:
                    row['ndc_min'] = [min(p[k] for p in ndc) for k in range(4)]
                    row['ndc_max'] = [max(p[k] for p in ndc) for k in range(4)]
            results.append(row)
            save('%d.json' % eid, row)
        save('meshes.json', results)
        save('status.json', dict(complete=True, events=list(EVENTS or (previous,))))
    except Exception:
        save('status.json', dict(complete=False, error=traceback.format_exc()))

capture = Path(pyrenderdoc.GetCaptureFilename())
previous = pyrenderdoc.CurEvent()
destination = capture.parent / 'capture-analysis' / capture.stem / 'native-motion'
def replay_native(controller):
    try:
        inspect_native(controller, destination)
    finally:
        controller.SetFrameEvent(previous, True)
pyrenderdoc.Replay().BlockInvoke(replay_native)
print('Native mesh inspection: ' + str(destination / 'status.json'))
