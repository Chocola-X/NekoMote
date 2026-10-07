"""Compare the actual session frame with the frozen one-atlas SDK draw protocol.

Only the diagnostic Chocola reference contract (SDK texture 2) is supported.
No reference coordinates or variables feed native evaluation. UVs associate
vertices; indexed triangle sets then verify tessellation as well as positions.
"""
import numpy as np


def compare(native, draws):
    frame = native['frame']
    vertices = np.array(frame['vertices'])
    if native['presentation_mirror']:
        vertices[:, 0] = -vertices[:, 0]
    indices = np.array(frame['indices'])
    textures = frame['textures']
    if len(textures) != 1:
        raise ValueError('this frozen reference has exactly one atlas')
    width, height = textures[0]['width'], textures[0]['height']
    uv = vertices[:, 2:4] * [width, height]
    source_draws = []
    for draw in draws:
        if draw.get('texture') != 2:
            continue
        if draw['mode'] not in (4, 5):
            raise ValueError('unexpected reference topology')
        source_uv = np.array(draw['attributes']['a_texCoord']['data'])[:, :2].copy()
        source_uv[:, 1] = height - source_uv[:, 1]
        source_draws.append((draw, source_uv, np.array(draw['attributes']['a_pos']['data'])))
    if not source_draws:
        raise ValueError('reference has no atlas draw')
    maximum = 0.0
    unmatched = unused = missing_triangles = unused_triangles = 0
    commands = []
    for command in frame['commands']:
        if command['texture'] != textures[0]['id']:
            raise ValueError('unexpected native atlas')
        raw_indices = indices[command['first_index']:
                              command['first_index'] + command['index_count']]
        unique = np.unique(raw_indices)
        points = uv[unique]
        low, high = points.min(axis=0) - .001, points.max(axis=0) + .001
        seen, reference_triangles = set(), set()
        native_triangles = {tuple(sorted(a)) for a in raw_indices.reshape(-1, 3)}
        command_maximum, count = 0.0, 0
        for draw, source_uv, positions in source_draws:
            selected = np.unique(draw['indices'])
            selected = selected[np.all((source_uv[selected] >= low) &
                                       (source_uv[selected] <= high), axis=1)]
            mapped = {}
            if len(selected):
                distances = np.linalg.norm(source_uv[selected, None, :] - points[None, :, :], axis=2)
                nearest = distances.argmin(axis=1)
                for row, (source_index, local_index) in enumerate(zip(selected, nearest)):
                    if distances[row, local_index] > .001:
                        unmatched += 1
                        continue
                    native_index = int(unique[local_index])
                    mapped[int(source_index)] = native_index
                    seen.add(native_index)
                    error = float(np.linalg.norm(vertices[native_index, :2] -
                                                 positions[source_index, :2]))
                    command_maximum = max(command_maximum, error)
                    count += 1
            raw = draw['indices']
            stride = 3 if draw['mode'] == 4 else 1
            for j in range(0, len(raw) - 2, stride):
                triangle = raw[j:j + 3]
                if all(i in mapped for i in triangle):
                    mapped_triangle = tuple(sorted(mapped[i] for i in triangle))
                    if len(set(mapped_triangle)) == 3:
                        reference_triangles.add(mapped_triangle)
        maximum = max(maximum, command_maximum)
        unused += len(set(map(int, unique)) - seen)
        missing_triangles += len(reference_triangles - native_triangles)
        unused_triangles += len(native_triangles - reference_triangles)
        commands.append(dict(samples=count, max_error_px=command_maximum))
    return dict(frame_max_error_px=maximum,
                unmatched_reference_vertices=unmatched,
                unused_native_vertices=unused,
                missing_reference_triangles=missing_triangles,
                unused_native_triangles=unused_triangles,
                commands=commands,
                passed=bool(maximum <= .001 and
                            unmatched == unused == missing_triangles == unused_triangles == 0))
