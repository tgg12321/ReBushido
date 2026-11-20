#!/usr/bin/env python3
"""A fake router on 127.0.0.1 for the tests of the window's port mapping
(src/port/window/portmap.c; tools/relay/test_portmap.py). Standard library
only.

    python3 tools/relay/fake_router.py [options]

It answers the three protocols portmap.c speaks:

  - UPnP: an SSDP responder (UDP), which answers an M-SEARCH for a gateway
    with the LOCATION of its description, and an HTTP server with an
    Internet Gateway Device's description and a WANIPConnection control
    URL that answers GetExternalIPAddress, AddPortMapping and
    DeletePortMapping;
  - PCP (MAP) and NAT-PMP (opcodes 0 and 1), on one UDP port, as a
    router's port 5351. With one of the two turned off, the port answers
    the other's version as a router without it does: a NAT-PMP router
    answers PCP's request with NAT-PMP's "unsupported version".

Its first line says where it listens:

    fake_router: ready ssdp=PORT http=PORT gateway=PORT

(a protocol turned off still has a port given, on which nothing listens),
then one line for each request it gets, for example

    fake_router: upnp AddPortMapping UDP 7845 -> 127.0.0.1:7845 lease 7200

It ends when its input closes, or after --seconds.

Options:
    --external A.B.C.D   the external address it reports (127.0.0.1)
    --no-upnp, --no-pmp, --no-pcp   that protocol turned off
    --no-ssdp            UPnP's HTTP server kept, its SSDP responder not
    --permanent-only     AddPortMapping with a lease other than 0 refused
                         with error 725 (OnlyPermanentLeasesSupported)
    --conflict PORT      AddPortMapping of external PORT refused with 718
                         (ConflictInMappingEntry): another PC has it
    --refuse CODE        every AddPortMapping refused with UPnP error CODE
    --chunked            the description sent in chunks (HTTP/1.1)
    --lease-cap N        PCP and NAT-PMP grant at most N seconds
    --seconds N          end after N seconds (120)
"""
import argparse
import http.server
import re
import socket
import struct
import sys
import threading
import time

HOST = '127.0.0.1'
LOCK = threading.Lock()
EPOCH_START = time.time()

SEARCH_TYPES = (
    'urn:schemas-upnp-org:device:InternetGatewayDevice:1',
    'urn:schemas-upnp-org:device:InternetGatewayDevice:2',
    'urn:schemas-upnp-org:service:WANIPConnection:1',
    'urn:schemas-upnp-org:service:WANIPConnection:2',
    'urn:schemas-upnp-org:service:WANPPPConnection:1',
    'ssdp:all',
    'upnp:rootdevice',
)
SERVICE = 'urn:schemas-upnp-org:service:WANIPConnection:1'
CONTROL = '/ctl/IPConn'

# An IGD's description as routers write it: the connection service two
# devices down, a service of another kind ahead of it, a relative URL.
DESCRIPTION = '''<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0">
<specVersion><major>1</major><minor>0</minor></specVersion>
<device>
<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>
<friendlyName>Fake router</friendlyName>
<serviceList>
<service>
<serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>
<serviceId>urn:upnp-org:serviceId:L3Forwarding1</serviceId>
<controlURL>/ctl/L3F</controlURL>
<eventSubURL>/evt/L3F</eventSubURL>
<SCPDURL>/L3F.xml</SCPDURL>
</service>
</serviceList>
<deviceList>
<device>
<deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType>
<serviceList>
<service>
<serviceType>urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1</serviceType>
<serviceId>urn:upnp-org:serviceId:WANCommonIFC1</serviceId>
<controlURL>/ctl/CmnIfCfg</controlURL>
<eventSubURL>/evt/CmnIfCfg</eventSubURL>
<SCPDURL>/WANCfg.xml</SCPDURL>
</service>
</serviceList>
<deviceList>
<device>
<deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType>
<serviceList>
<service>
<ServiceType>%s</ServiceType>
<serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId>
<controlURL> %s </controlURL>
<eventSubURL>/evt/IPConn</eventSubURL>
<SCPDURL>/WANIPCn.xml</SCPDURL>
</service>
</serviceList>
</device>
</deviceList>
</device>
</deviceList>
</device>
</root>
''' % (SERVICE, CONTROL)

ENVELOPE = ('<?xml version="1.0"?>\r\n'
            '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
            's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body>%s</s:Body></s:Envelope>\r\n')
FAULT = ('<s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring><detail>'
         '<UPnPError xmlns="urn:schemas-upnp-org:control-1-0"><errorCode>%d</errorCode>'
         '<errorDescription>%s</errorDescription></UPnPError></detail></s:Fault>')


def log(text):
    with LOCK:
        sys.stdout.write('fake_router: %s\n' % text)
        sys.stdout.flush()


def free_port(kind):
    """A port of 127.0.0.1 nothing listens on."""
    probe = socket.socket(socket.AF_INET, kind)
    probe.bind((HOST, 0))
    port = probe.getsockname()[1]
    probe.close()
    return port


def epoch():
    return int(time.time() - EPOCH_START) & 0xFFFFFFFF


def tag(body, name):
    found = re.search(r'<(?:\w+:)?%s>(.*?)</(?:\w+:)?%s>' % (name, name), body, re.S | re.I)
    return found.group(1).strip() if found else ''


def ssdp_loop(sock, http_port):
    while True:
        data, peer = sock.recvfrom(2048)
        text = data.decode('latin-1')
        if not text.upper().startswith('M-SEARCH'):
            continue
        search_type = ''
        for line in text.split('\r\n'):
            if line.upper().startswith('ST:'):
                search_type = line[3:].strip()
        log('upnp M-SEARCH %s' % search_type)
        if search_type not in SEARCH_TYPES:
            continue
        answer = ('HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nEXT:\r\n'
                  'Location: http://%s:%d/rootDesc.xml\r\nSERVER: Fake/1.0 UPnP/1.0 fake_router/1.0\r\n'
                  'st: %s\r\nUSN: uuid:fake-router::%s\r\n\r\n' % (HOST, http_port, search_type, search_type))
        sock.sendto(answer.encode('latin-1'), peer)


def gateway_loop(sock, args):
    """PCP and NAT-PMP, on one port."""
    external = socket.inet_aton(args.external)
    while True:
        data, peer = sock.recvfrom(2048)
        if len(data) < 2:
            continue
        version, opcode = data[0], data[1]
        if version == 2:
            if args.no_pcp:
                log('pcp request answered as NAT-PMP: unsupported version')
                sock.sendto(struct.pack('>BBHI', 0, 0x80 | opcode, 1, epoch()), peer)
                continue
            if opcode != 1 or len(data) < 60:
                log('pcp malformed request')
                continue
            lifetime = struct.unpack('>I', data[4:8])[0]
            nonce = data[24:36]
            protocol = data[36]
            internal, suggested = struct.unpack('>HH', data[40:44])
            if args.lease_cap is not None:
                lifetime = min(lifetime, args.lease_cap)
            assigned = suggested or internal
            client = socket.inet_ntoa(data[20:24])
            if lifetime == 0:
                log('pcp delete %s %d (client %s)' % ('UDP' if protocol == 17 else protocol, internal, client))
            else:
                log('pcp map %s %d -> %d lease %d (client %s)'
                    % ('UDP' if protocol == 17 else protocol, internal, assigned, lifetime, client))
            answer = (struct.pack('>BBBBII', 2, 0x81, 0, 0, lifetime, epoch()) + bytes(12) + nonce
                      + struct.pack('>B3xHH', protocol, internal, assigned)
                      + bytes(10) + b'\xff\xff' + external)
            sock.sendto(answer, peer)
        elif version == 0:
            if args.no_pmp:
                log('pmp request answered as PCP: unsupported version')
                sock.sendto(struct.pack('>BBBBII', 2, 0x80 | opcode, 0, 1, 0, epoch()) + bytes(16), peer)
                continue
            if opcode == 0:
                log('pmp external address')
                sock.sendto(struct.pack('>BBHI', 0, 128, 0, epoch()) + external, peer)
            elif opcode in (1, 2) and len(data) >= 12:
                internal, suggested, lifetime = struct.unpack('>HHI', data[4:12])
                if args.lease_cap is not None:
                    lifetime = min(lifetime, args.lease_cap)
                name = 'UDP' if opcode == 1 else 'TCP'
                if lifetime == 0:
                    log('pmp delete %s %d' % (name, internal))
                    assigned = 0
                else:
                    assigned = suggested or internal
                    log('pmp map %s %d -> %d lease %d' % (name, internal, assigned, lifetime))
                sock.sendto(struct.pack('>BBHIHHI', 0, 128 + opcode, 0, epoch(), internal, assigned, lifetime), peer)
            else:
                log('pmp unsupported opcode %d' % opcode)
                sock.sendto(struct.pack('>BBHI', 0, 128 + opcode, 5, epoch()), peer)


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    args = None

    def log_message(self, format, *items):
        pass

    def send_text(self, status, body, chunked=False):
        data = body.encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'text/xml; charset="utf-8"')
        self.send_header('Connection', 'close')
        if chunked:
            self.send_header('Transfer-Encoding', 'chunked')
            self.end_headers()
            step = 97
            for at in range(0, len(data), step):
                piece = data[at:at + step]
                self.wfile.write(b'%x;piece\r\n' % len(piece) + piece + b'\r\n')
            self.wfile.write(b'0\r\n\r\n')
        else:
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        self.close_connection = True

    def do_GET(self):
        log('upnp GET %s' % self.path)
        if self.path == '/rootDesc.xml':
            self.send_text(200, DESCRIPTION, chunked=self.args.chunked)
        else:
            self.send_text(404, '')

    def do_POST(self):
        length = int(self.headers.get('Content-Length', '0'))
        body = self.rfile.read(length).decode('utf-8', 'replace')
        action = self.headers.get('SOAPAction', '').strip('"').split('#')[-1]
        if self.path != CONTROL:
            log('upnp POST %s %s (not the control URL)' % (self.path, action))
            self.send_text(404, '')
            return
        if action == 'GetExternalIPAddress':
            log('upnp GetExternalIPAddress')
            self.send_text(200, ENVELOPE % (
                '<u:GetExternalIPAddressResponse xmlns:u="%s"><NewExternalIPAddress>%s'
                '</NewExternalIPAddress></u:GetExternalIPAddressResponse>' % (SERVICE, self.args.external)))
        elif action == 'AddPortMapping':
            external = int(tag(body, 'NewExternalPort') or 0)
            line = 'upnp AddPortMapping %s %d -> %s:%s lease %s' % (
                tag(body, 'NewProtocol'), external, tag(body, 'NewInternalClient'), tag(body, 'NewInternalPort'),
                tag(body, 'NewLeaseDuration'))
            refusal = None
            if self.args.refuse is not None:
                refusal = (self.args.refuse, 'Refused')
            elif self.args.permanent_only and tag(body, 'NewLeaseDuration') not in ('0', ''):
                refusal = (725, 'OnlyPermanentLeasesSupported')
            elif self.args.conflict == external:
                refusal = (718, 'ConflictInMappingEntry')
            if tag(body, 'NewPortMappingDescription') != 'ReBushido':
                line += ' description %r' % tag(body, 'NewPortMappingDescription')
            if refusal:
                log('%s refused %d' % (line, refusal[0]))
                self.send_text(500, ENVELOPE % (FAULT % refusal))
            else:
                log(line)
                self.send_text(200, ENVELOPE % ('<u:AddPortMappingResponse xmlns:u="%s"/>' % SERVICE))
        elif action == 'DeletePortMapping':
            log('upnp DeletePortMapping %s %s' % (tag(body, 'NewProtocol'), tag(body, 'NewExternalPort')))
            self.send_text(200, ENVELOPE % ('<u:DeletePortMappingResponse xmlns:u="%s"/>' % SERVICE))
        else:
            log('upnp %s (unknown)' % action)
            self.send_text(500, ENVELOPE % (FAULT % (401, 'Invalid Action')))


def main():
    parser = argparse.ArgumentParser(description='A fake router for the port mapping tests.')
    parser.add_argument('--external', default='127.0.0.1')
    parser.add_argument('--no-upnp', action='store_true')
    parser.add_argument('--no-ssdp', action='store_true')
    parser.add_argument('--no-pmp', action='store_true')
    parser.add_argument('--no-pcp', action='store_true')
    parser.add_argument('--permanent-only', action='store_true')
    parser.add_argument('--conflict', type=int, default=None)
    parser.add_argument('--refuse', type=int, default=None)
    parser.add_argument('--chunked', action='store_true')
    parser.add_argument('--lease-cap', type=int, default=None)
    parser.add_argument('--seconds', type=float, default=120)
    args = parser.parse_args()

    threads = []
    if args.no_upnp:
        ssdp_port, http_port = free_port(socket.SOCK_DGRAM), free_port(socket.SOCK_STREAM)
    else:
        Handler.args = args
        server = http.server.ThreadingHTTPServer((HOST, 0), Handler)
        http_port = server.server_address[1]
        threads.append(threading.Thread(target=server.serve_forever, daemon=True))
        if args.no_ssdp:
            ssdp_port = free_port(socket.SOCK_DGRAM)
        else:
            ssdp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            ssdp.bind((HOST, 0))
            ssdp_port = ssdp.getsockname()[1]
            threads.append(threading.Thread(target=ssdp_loop, args=(ssdp, http_port), daemon=True))
    if args.no_pcp and args.no_pmp:
        gateway_port = free_port(socket.SOCK_DGRAM)
    else:
        gateway = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        gateway.bind((HOST, 0))
        gateway_port = gateway.getsockname()[1]
        threads.append(threading.Thread(target=gateway_loop, args=(gateway, args), daemon=True))
    for thread in threads:
        thread.start()
    log('ready ssdp=%d http=%d gateway=%d' % (ssdp_port, http_port, gateway_port))

    ended = threading.Event()

    def watch_input():
        try:
            sys.stdin.read()
        except (OSError, ValueError):
            return
        ended.set()

    threading.Thread(target=watch_input, daemon=True).start()
    ended.wait(args.seconds)


if __name__ == '__main__':
    main()
