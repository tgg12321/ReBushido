"""The release's signature (tools/release_sign.py, tools/ed25519.py): RFC 8032's cases, and a manifest refused when changed

Ed25519 in Python alone is held to the four cases of RFC 8032, 7.1 (the
public key, the signature, and its check), and a changed signature, a
changed message and another key are refused. A manifest is made of a
package made up for the test, signed, and verified; then one byte of it
changed is refused. The PC program checks the same signatures with
Monocypher (tools/port_update_test.py)."""
import io
import os
import zipfile

import release_sign


def run(checks):
    checks.equal('the self-test\'s failures', release_sign.self_test(), [])
    folder = 'ReBushido-PC-9.9-0000000'
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr(folder + '/rebushido.exe', b'MZ made up')
        z.writestr(folder + '/mods/ui/mod.txt', b'name = ui\n')
    work = os.path.join(release_sign.ROOT, 'build', 'tests', 'release_sign')
    os.makedirs(work, exist_ok=True)
    path = os.path.join(work, folder + '.zip')
    with open(path, 'wb') as f:
        f.write(out.getvalue())
    text = release_sign.manifest_text(path, ['a note'])
    os.remove(path)
    secret = bytes(range(1, 33))
    public = release_sign.ed25519.public_key(secret)
    signature = release_sign.sign(text, secret)
    checks.true('the manifest names its version', 'version = 9.9\n' in text)
    checks.true('the manifest names both files', text.count('\nfile = ') == 2)
    checks.true('the manifest verifies', release_sign.verify(text.encode(), signature, public))
    checks.true('a changed manifest is refused',
                not release_sign.verify(text.replace('9.9', '9.8').encode(), signature, public))
