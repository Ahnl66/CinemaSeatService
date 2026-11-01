#!/usr/bin/env python3
"""Independent, hash-verified dependencies. No reference to the app/vendor tree."""
import argparse, hashlib, json, os, shutil, subprocess, tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = json.loads((ROOT / 'dependencies/manifest.json').read_text())

def build(scratch, test_codecs=False):
    scratch = Path(scratch).resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    prefix = scratch / ('test-deps' if test_codecs else 'deps')
    stamp = hashlib.sha256((json.dumps(MANIFEST, sort_keys=True) + str(test_codecs) + 'build-v4-strict-xll').encode()).hexdigest()
    if (prefix / 'stamp').exists() and (prefix / 'stamp').read_text() == stamp:
        return prefix
    include = prefix / 'include'
    include.mkdir(parents=True, exist_ok=True)
    sources = {}
    for name, entry in MANIFEST.items():
        suffix = '.hpp' if name == 'json' else ('.tar.gz' if name == 'ffmpeg' else '.tar.bz2')
        archive = scratch / (name + suffix)
        if not archive.exists():
            subprocess.run(['curl', '--fail', '--location', '--retry', '2', '--max-time', '180', entry['url'], '-o', str(archive)], check=True)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != entry['sha256']:
            raise RuntimeError(name + ': dependency hash mismatch')
        if name == 'json':
            (include / 'nlohmann').mkdir(exist_ok=True)
            shutil.copy2(archive, include / 'nlohmann/json.hpp')
        else:
            destination = scratch / (name + '-source')
            if not (destination / '.complete').exists():
                destination.mkdir(exist_ok=True)
                with tarfile.open(archive) as tar:
                    for member in tar.getmembers():
                        target = (destination / member.name).resolve()
                        if not target.is_relative_to(destination) or member.isdev():
                            raise RuntimeError('Unsafe archive member')
                        if member.issym() or member.islnk():
                            link = (target.parent / member.linkname).resolve() if member.issym() else (destination / member.linkname).resolve()
                            if not link.is_relative_to(destination):
                                raise RuntimeError('Unsafe archive link')
                    tar.extractall(destination)
                (destination / '.complete').touch()
            source, = [p for p in destination.iterdir() if p.is_dir()]
            sources[name] = source
    if not (include / 'boost').exists():
        shutil.copytree(sources['boost'] / 'boost', include / 'boost')
    # Refuse FFmpeg's successful lossy XLL recovery; no silent core output.
    xll = sources['ffmpeg'] / 'libavcodec/dca_xll.c'
    original = 'if (dca->packet & DCA_PACKET_RECOVERY) {'
    strict = original + '\n        return AVERROR_INVALIDDATA; /* CSAD: reject lossy XLL recovery */'
    text = xll.read_text()
    if strict not in text:
        if text.count(original) != 1:
            raise RuntimeError('Pinned XLL recovery patch no longer applies')
        xll.write_text(text.replace(original, strict))
    directory = scratch / ('test-ffmpeg-build' if test_codecs else 'ffmpeg-build')
    directory.mkdir(exist_ok=True)
    flags = ['--disable-everything', '--disable-autodetect', '--disable-programs', '--disable-doc',
             '--disable-network', '--disable-avformat', '--disable-avdevice', '--disable-avfilter',
             '--disable-swscale', '--disable-swresample', '--disable-static', '--enable-shared',
             '--disable-asm', '--disable-x86asm', '--enable-avcodec', '--enable-avutil',
             '--enable-decoder=dca,truehd,mlp' + (',flac' if test_codecs else ''),
             '--enable-encoder=flac' + (',dca,truehd,mlp' if test_codecs else ''),
             '--prefix=' + str(prefix)]
    with (directory / 'build.log').open('w') as log:
        subprocess.run([str(sources['ffmpeg'] / 'configure'), *flags], cwd=directory, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(['make', '-j' + str(min(8, os.cpu_count() or 2))], cwd=directory, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(['make', 'install'], cwd=directory, stdout=log, stderr=subprocess.STDOUT, check=True)
    shutil.copy2(ROOT / 'dependencies/manifest.json', prefix / 'manifest.json')
    (prefix / 'stamp').write_text(stamp)
    return prefix

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--scratch', type=Path, required=True)
    parser.add_argument('--test-codecs', action='store_true')
    args = parser.parse_args()
    print(build(args.scratch, args.test_codecs))
