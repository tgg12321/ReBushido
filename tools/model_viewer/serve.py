"""The fighters' models side by side in a browser, with notes for the agents.

    python tools/model_viewer/serve.py            then open http://127.0.0.1:8765/

Every version of a fighter's model found under tmp/ is offered (the game's
own, the hd mod, each remade pass, and any folder of tmp/models/versions/),
drawn side by side with one camera. A note typed on the page is appended to
tmp/models/feedback/<FIGHTER>.md with the versions shown and a picture of
the view, for the agent remaking that fighter to read.

Nothing here is committed but this tool: the models are made from the
player's disc and stay in tmp/.
"""

import base64
import datetime
import glob
import json
import os
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.join(ROOT, 'tmp')
FEEDBACK = os.path.join(TMP, 'models', 'feedback')
PORT = 8765

# (label, glob of the fighter's file with {f} for its name), in the order shown
SOURCES = [
    ('game', 'models/ref/{f}.glb'),
    ('hd', 'mods/hd/models/{f}.glb'),
    ('remade 1', 'models/ref/remade1/{f}.glb'),
    ('remade (mod)', 'mods/remade/models/{f}.glb'),
]
VERSIONS = 'models/versions/*/{f}.glb'  # one folder per pass: fable-01, ...
NAME = re.compile(r'^M\d{3}$')


def fighters():
    names = set()
    for _, pattern in SOURCES + [('', VERSIONS)]:
        for path in glob.glob(os.path.join(TMP, pattern.format(f='M[0-9][0-9][0-9]'))):
            names.add(os.path.splitext(os.path.basename(path))[0])
    return sorted(names)


def versions(fighter):
    found = []
    for label, pattern in SOURCES:
        path = os.path.join(TMP, pattern.format(f=fighter))
        if os.path.isfile(path):
            found.append((label, path))
    for path in sorted(glob.glob(os.path.join(TMP, VERSIONS.format(f=fighter))),
                       key=os.path.getmtime):
        found.append((os.path.basename(os.path.dirname(path)), path))
    out = [{'label': label, 'url': '/model/%s/%d' % (fighter, i),
             'changed': datetime.datetime.fromtimestamp(os.path.getmtime(path)).strftime('%Y-%m-%d %H:%M')}
            for i, (label, path) in enumerate(found)]
    # the game's own model once more, drawn as the PlayStation drew it
    for v in out:
        if v['label'] == 'game':
            out.insert(0, dict(v, label='original (PS1)', ps1=True))
            break
    return out, [p for _, p in found]


def motions(fighter):
    """The fights the model pipeline recorded of this fighter (each bone's
    place a frame: tools/port_models/motions.py), as (label, path)."""
    out = []
    for path in sorted(glob.glob(os.path.join(TMP, 'models', 'ref', 'motions', '*', fighter + '.*.motion.txt'))):
        out.append((os.path.basename(os.path.dirname(path)), path))
    return out


class Handler(BaseHTTPRequestHandler):
    def send(self, code, body, kind):
        self.send_response(code)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split('?')[0]
        if path == '/':
            with open(os.path.join(HERE, 'index.html'), 'rb') as f:
                return self.send(200, f.read(), 'text/html; charset=utf-8')
        if path == '/api/fighters':
            return self.send(200, json.dumps(fighters()).encode(), 'application/json')
        m = re.match(r'^/api/versions/(M\d{3})$', path)
        if m:
            return self.send(200, json.dumps(versions(m.group(1))[0]).encode(), 'application/json')
        m = re.match(r'^/api/feedback/(M\d{3})$', path)
        if m:
            note = os.path.join(FEEDBACK, m.group(1) + '.md')
            text = open(note, encoding='utf-8').read() if os.path.isfile(note) else ''
            return self.send(200, text.encode('utf-8'), 'text/plain; charset=utf-8')
        m = re.match(r'^/api/motions/(M\d{3})$', path)
        if m:
            return self.send(200, json.dumps([label for label, _ in motions(m.group(1))]).encode(), 'application/json')
        m = re.match(r'^/motion/(M\d{3})/(\d+)$', path)
        if m:
            found = motions(m.group(1))
            i = int(m.group(2))
            if i < len(found):
                with open(found[i][1], 'rb') as f:
                    return self.send(200, f.read(), 'text/plain')
        m = re.match(r'^/model/(M\d{3})/(\d+)$', path)
        if m:
            paths = versions(m.group(1))[1]
            i = int(m.group(2))
            if i < len(paths):
                with open(paths[i], 'rb') as f:
                    return self.send(200, f.read(), 'model/gltf-binary')
        self.send(404, b'not found', 'text/plain')

    def do_POST(self):
        m = re.match(r'^/api/feedback/(M\d{3})$', self.path)
        if not m:
            return self.send(404, b'not found', 'text/plain')
        data = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        fighter = m.group(1)
        os.makedirs(FEEDBACK, exist_ok=True)
        stamp = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        picture = ''
        shot = data.get('picture', '')
        if shot.startswith('data:image/png;base64,'):
            name = '%s-%s.png' % (fighter, datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
            with open(os.path.join(FEEDBACK, name), 'wb') as f:
                f.write(base64.b64decode(shot.split(',', 1)[1]))
            picture = '\n\nThe view: `tmp/models/feedback/%s`' % name
        with open(os.path.join(FEEDBACK, fighter + '.md'), 'a', encoding='utf-8', newline='') as f:
            f.write('## %s, the owner\n\nShown: %s; pose: %s.\n\n%s%s\n\n' % (
                stamp, ', '.join(data.get('shown', [])), data.get('pose', 'bind pose'),
                data.get('text', '').strip(), picture))
        self.send(200, b'ok', 'text/plain')

    def log_message(self, *args):
        pass


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else PORT
    print('the models side by side: http://127.0.0.1:%d/  (Ctrl+C ends it)' % port)
    ThreadingHTTPServer(('127.0.0.1', port), Handler).serve_forever()


if __name__ == '__main__':
    main()
