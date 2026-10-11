"""Exercise real signer failure paths; temporary test keys never leave tempdir."""
from pathlib import Path
import subprocess
import sys
import tempfile

signer, public, message, signature = sys.argv[1:]
checks = 0

def run(*args, good=True):
    global checks
    result = subprocess.run([signer, *map(str, args)], capture_output=True)
    checks += 1
    assert (result.returncode == 0) == good, f"unexpected result for {args[0]}"
    return result.stdout

run('verify', public, message, signature)
with tempfile.TemporaryDirectory(prefix='feralcat-sign-test-') as temporary:
    root = Path(temporary)
    assert root.resolve().parent == Path(tempfile.gettempdir()).resolve()
    assert root.name.startswith('feralcat-sign-test-')
    seed, pub, sig = root/'test.seed.bin', root/'test.pub', root/'test.sig'
    run('keygen', seed, pub)
    assert seed.stat().st_size == 32
    before = run('pub', seed)
    run('keygen', seed, good=False)
    assert run('pub', seed) == before  # overwrite guard preserves identity
    run('sign', seed, message, sig)
    run('verify', pub, message, sig)
    run('verify', public, message, sig, good=False)
    run('sign', seed, message, seed, good=False)  # cannot replace seed with output
    assert run('pub', seed) == before
    changed = root/'changed.bin'
    changed.write_bytes(Path(message).read_bytes() + b'x')
    run('verify', pub, changed, sig, good=False)
    for size in (0, 63, 65):
        invalid = root/f'invalid-{size}.sig'
        invalid.write_bytes(bytes(size))
        run('verify', pub, message, invalid, good=False)
    run('verify', 'z'*64, message, sig, good=False)
    run('verify', pub, root/'missing-input', sig, good=False)
    run('verify', pub, message, root/'missing-signature', good=False)
    invalid_seed = root/'bad.seed.bin'
    invalid_seed.write_bytes(bytes(31))
    run('sign', invalid_seed, message, root/'bad.sig', good=False)
    empty = root/'empty'
    empty.write_bytes(b'')
    run('sign', seed, empty, root/'empty.sig', good=False)
    oversized = root/'oversized'
    oversized.write_bytes(bytes(512*1024+1))
    run('sign', seed, oversized, root/'oversized.sig', good=False)
print(f'{checks} signer checks passed')
