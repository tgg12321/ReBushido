"""The remake's recipe of each fighter (fighters.json) and where things are."""
import json
import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def recipe_of(name):
    with open(os.path.join(HERE, 'fighters.json'), encoding='utf-8') as f:
        data = json.load(f)
    r = dict(data['default'])
    for k, v in data['fighters'].get(name, {}).items():
        if isinstance(v, dict) and isinstance(r.get(k), dict):
            r[k] = dict(r[k], **v)
        else:
            r[k] = v
    r['replace'] = {k: int(v) for k, v in r['replace'].items()}
    if isinstance(r.get('materials'), dict):
        r['materials'] = {k: v for k, v in r['materials'].items() if k != 'about'}
    return r


def main_checkout():
    """The main checkout of a worktree, where the disc and the mods are
    kept; this checkout where it is the main one."""
    marker = os.sep + os.path.join('.claude', 'worktrees') + os.sep
    if marker in REPO + os.sep:
        return REPO[:REPO.index(marker)]
    try:
        common = subprocess.run(['git', 'rev-parse', '--git-common-dir'], cwd=REPO,
                                capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return REPO
    return os.path.dirname(os.path.abspath(os.path.join(REPO, common)))
