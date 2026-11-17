"""The pieces of music tests/port/music.c plays, made here, and what it says checked.

Two WAVs of whole numbers (one with a loop in its `smpl` chunk) and, where
the system has libvorbisenc (it is the reference encoder of Ogg Vorbis, and
the test is skipped in part without it), an Ogg Vorbis file with loop tags:
a sine of a known pitch, at 48,000 samples a second. The C decodes it with
the port's stb_vorbis (src/port/vorbis.c) and says how long it is, its
rate, its tags and how often it crosses zero, which is checked here
against what was encoded.
"""
import ctypes
import math
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FOLDER = os.path.join(ROOT, 'build', 'tests', 'music')

WAV_FRAMES = 20000
LOOP_START, LOOP_END = 1000, 3000      # the smpl chunk's loop: its last sample is LOOP_END - 1
OGG_RATE = 48000
OGG_FRAMES = 48000                     # a second
OGG_PITCH = 1000                       # Hz
OGG_LOOP_START, OGG_LOOP_LENGTH = 12000, 24000


def flags():
    """src/port/vorbis.c compiled as tools/port_build.py compiles it."""
    import port_build  # tools/port_build.py, on the path tests/run.py gives
    return port_build.PORT_OWN_FLAGS['src/port/vorbis.c']


def wav_sample(i, side):
    """A sample of the WAVs: numbers that say where they are."""
    return ((i * 37 + side * 11) % 2000) * 16 - 16000


def write_wav(path, loop):
    data = b''.join(struct.pack('<hh', wav_sample(i, 0), wav_sample(i, 1)) for i in range(WAV_FRAMES))
    fmt = struct.pack('<HHIIHH', 1, 2, 44100, 44100 * 4, 4, 16)
    chunks = b'fmt ' + struct.pack('<I', len(fmt)) + fmt
    # a chunk the reader passes over, of an odd length
    chunks += b'LIST' + struct.pack('<I', 3) + b'abc' + b'\0'
    chunks += b'data' + struct.pack('<I', len(data)) + data
    if loop:
        smpl = struct.pack('<9I', 0, 0, 22675, 60, 0, 0, 0, 1, 0)
        smpl += struct.pack('<6I', 0, 0, LOOP_START, LOOP_END - 1, 0, 0)
        chunks += b'smpl' + struct.pack('<I', len(smpl)) + smpl
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 4 + len(chunks)) + b'WAVE' + chunks)


def write_ogg(path):
    """A mono sine through libvorbisenc, or False if the system has none."""
    try:
        ogg = ctypes.CDLL('libogg.so.0')
        vorbis = ctypes.CDLL('libvorbis.so.0')
        enc = ctypes.CDLL('libvorbisenc.so.2')
    except OSError:
        return False

    class Page(ctypes.Structure):
        _fields_ = [('header', ctypes.c_void_p), ('header_len', ctypes.c_long),
                    ('body', ctypes.c_void_p), ('body_len', ctypes.c_long)]

    class Packet(ctypes.Structure):
        _fields_ = [('packet', ctypes.c_void_p), ('bytes', ctypes.c_long), ('b_o_s', ctypes.c_long),
                    ('e_o_s', ctypes.c_long), ('granulepos', ctypes.c_int64), ('packetno', ctypes.c_int64)]

    info, comment, dsp, block, stream = [ctypes.create_string_buffer(8192) for _ in range(5)]
    vorbis.vorbis_info_init(info)
    if enc.vorbis_encode_init_vbr(info, ctypes.c_long(1), ctypes.c_long(OGG_RATE), ctypes.c_float(0.4)) != 0:
        return False
    vorbis.vorbis_comment_init(comment)
    vorbis.vorbis_comment_add_tag(comment, b'LOOPSTART', str(OGG_LOOP_START).encode())
    vorbis.vorbis_comment_add_tag(comment, b'looplength', str(OGG_LOOP_LENGTH).encode())
    vorbis.vorbis_analysis_init(dsp, info)
    vorbis.vorbis_block_init(dsp, block)
    ogg.ogg_stream_init(stream, 7)
    headers = [Packet(), Packet(), Packet()]
    vorbis.vorbis_analysis_headerout(dsp, comment, *[ctypes.byref(h) for h in headers])
    for h in headers:
        ogg.ogg_stream_packetin(stream, ctypes.byref(h))
    page = Page()
    packet = Packet()
    out = bytearray()

    def take(page):
        out.extend(ctypes.string_at(page.header, page.header_len))
        out.extend(ctypes.string_at(page.body, page.body_len))

    while ogg.ogg_stream_flush(stream, ctypes.byref(page)):
        take(page)
    vorbis.vorbis_analysis_buffer.restype = ctypes.POINTER(ctypes.POINTER(ctypes.c_float))

    def drain():
        while vorbis.vorbis_analysis_blockout(dsp, block) == 1:
            vorbis.vorbis_analysis(block, None)
            vorbis.vorbis_bitrate_addblock(block)
            while vorbis.vorbis_bitrate_flushpacket(dsp, ctypes.byref(packet)):
                ogg.ogg_stream_packetin(stream, ctypes.byref(packet))
                while ogg.ogg_stream_pageout(stream, ctypes.byref(page)):
                    take(page)

    done = 0
    while done < OGG_FRAMES:
        count = min(4096, OGG_FRAMES - done)
        buffer = vorbis.vorbis_analysis_buffer(dsp, count)
        for i in range(count):
            buffer[0][i] = 0.5 * math.sin(2 * math.pi * OGG_PITCH * (done + i) / OGG_RATE)
        vorbis.vorbis_analysis_wrote(dsp, count)
        done += count
        drain()
    vorbis.vorbis_analysis_wrote(dsp, 0)
    drain()
    with open(path, 'wb') as f:
        f.write(bytes(out))
    return True


def arguments():
    os.makedirs(FOLDER, exist_ok=True)
    write_wav(os.path.join(FOLDER, 'looped.wav'), True)
    write_wav(os.path.join(FOLDER, 'once.wav'), False)
    ogg = os.path.join(FOLDER, 'tone.ogg')
    return [os.path.join(FOLDER, 'looped.wav'), os.path.join(FOLDER, 'once.wav'),
            ogg if write_ogg(ogg) else 'none']


def check(lines, checks):
    seen = False
    for line in lines:
        words = line.split()
        if words[0] == 'ogg' and words[1] == 'none':
            checks.note('no libvorbisenc here: the Ogg decoder was not run on a file')
            seen = True
        elif words[0] == 'ogg':
            values = dict(zip(words[1::2], (int(w) for w in words[2::2])))
            checks.equal('the Ogg file\'s rate', values['rate'], OGG_RATE)
            checks.equal('its channels', values['channels'], 1)
            checks.equal('its length in samples', values['frames'], OGG_FRAMES)
            checks.equal('its samples decoded', values['decoded'], OGG_FRAMES)
            checks.equal('its loop\'s start, from LOOPSTART', values['loop_start'], OGG_LOOP_START)
            checks.equal('its loop\'s end, from looplength', values['loop_end'], OGG_LOOP_START + OGG_LOOP_LENGTH)
            # a sine crosses zero twice a cycle
            want = 2 * OGG_PITCH * OGG_FRAMES // OGG_RATE
            checks.near('the times it crosses zero, for a sine of %d Hz' % OGG_PITCH, values['crossings'], want, 4)
            # half of full scale, give or take what the encoder does
            checks.near('its loudest sample, half of full scale', values['peak'], 16384, 2500)
            seen = True
    checks.true('the C side said what it decoded', seen)
