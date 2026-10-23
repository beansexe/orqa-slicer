import pathlib
import struct
import sys
import warnings
import zipfile
import zlib

root = pathlib.Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)
def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(b'\x00\x00\x00\x00\xff')) + chunk(b'IEND', b'')
(root / 'thumbnail.png').write_bytes(png)
(root / 'bad.png').write_bytes(png[:24])
model = '<config><plate><metadata key="plater_id" value="1"/><metadata key="gcode_file" value="Metadata/plate_1.gcode"/></plate></config>'
slice_info = '<config><plate><metadata key="index" value="1"/><metadata key="printer_model_id" value="Flashforge Adventurer 5M"/><metadata key="nozzle_diameters" value="0.4"/></plate></config>'
entries = {'[Content_Types].xml': '<Types/>', 'Metadata/model_settings.config': model, 'Metadata/slice_info.config': slice_info, 'Metadata/plate_1.gcode': 'G1 X10 Y10 E1\n'}
def archive(name, values, duplicate=None):
    with warnings.catch_warnings():
        warnings.simplefilter('ignore', UserWarning)
        with zipfile.ZipFile(root / (name + '.3mf'), 'w', zipfile.ZIP_DEFLATED) as out:
            for key, value in values.items(): out.writestr(key, value)
            if duplicate: out.writestr(duplicate, values[duplicate])
archive('valid', entries)
archive('missing-gcode', {k:v for k,v in entries.items() if not k.endswith('.gcode')})
archive('model-only', {'[Content_Types].xml':'<Types/>', '3D/3dmodel.model':'<model/>'})
archive('wrong-model', dict(entries, **{'Metadata/slice_info.config':slice_info.replace('Adventurer 5M', 'Other')}))
archive('wrong-nozzle', dict(entries, **{'Metadata/slice_info.config':slice_info.replace('0.4', '0.8')}))
archive('wrong-plate', dict(entries, **{'Metadata/slice_info.config':slice_info.replace('value="1"', 'value="2"')}))
archive('duplicate-entry', entries, 'Metadata/plate_1.gcode')
archive('bad-xml', dict(entries, **{'Metadata/slice_info.config':'<config><plate>'}))
archive('dtd', dict(entries, **{'Metadata/slice_info.config':'<!DOCTYPE config [<!ENTITY x "test">]>' + slice_info}))
archive('duplicate-metadata', dict(entries, **{'Metadata/slice_info.config':slice_info.replace('</plate>', '<metadata key="index" value="1"/></plate>')}))
archive('no-motion', dict(entries, **{'Metadata/plate_1.gcode':'G28\nM104 S200\n'}))
archive('bad-reference', dict(entries, **{'Metadata/model_settings.config':model.replace('plate_1.gcode', 'plate_2.gcode')}))
archive('oversized-xml', dict(entries, **{'Metadata/slice_info.config':' ' * (2 * 1024 * 1024 + 1)}))
archive('mutated', entries)
print('Created bounded cloud archive fixtures.')
