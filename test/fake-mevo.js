// A simulated FlightScope Mevo+ for testing the direct connection (lib/mevo.js) without the device:
// answers the handshake as documented by ironsight, arms (refusing the first attempt, like the
// real thing), and fires shots on request.
//
// Standalone: node test/fake-mevo.js [port=5100] [seconds between shots=15]
import net from 'node:net';
import { pathToFileURL } from 'node:url';
import { BUS, encodeFrame, FrameReader } from '../lib/mevo.js';

const int24 = (v) => { const u = v & 0xffffff; return [u >> 16, (u >> 8) & 255, u & 255]; };
const int16 = (v) => { const u = v & 0xffff; return [u >> 8, u & 255]; };
export function flightPayload() {
  const p = new Array(157).fill(0);
  p[0] = 0x9c;
  const put = (o, v) => p.splice(o, 3, ...int24(v));
  put(1, 7); put(16, 67200); put(19, -1300); put(22, 12000); put(25, 200500); put(28, 6200); put(31, 30000);
  put(43, 2700); put(46, -450); put(85, 45000);
  return p;
}
function clubPayload() {
  const p = new Array(167).fill(0);
  p[0] = 0xa6;
  const put = (o, v) => p.splice(o, 3, ...int24(v));
  put(5, 4500); put(11, -210); put(14, -350); put(17, 120); put(20, 1840); put(23, 1490);
  return p;
}
function spinPayload() {
  const p = new Array(138).fill(0);
  p[0] = 0x89;
  p.splice(108, 2, ...int16(2737)); p.splice(132, 2, ...int16(-95));
  return p;
}
const ascii = (s, n) => { const b = [...Buffer.from(s, 'latin1')]; while (b.length < n) b.push(0); return b; };

export function fakeMevo() {
  const seen = { modes: [], arms: 0, acks: 0 };
  const server = net.createServer((sock) => {
    const r = new FrameReader();
    const reply = (src, type, payload) => sock.write(encodeFrame(BUS.APP, type, payload, src));
    const text = (src, s) => reply(src, 0xe3, ascii(s, s.length + 1));
    sock.on('data', (chunk) => {
      for (const f of r.push(chunk)) {
        const to = f.dest, p = [...f.payload];
        switch (f.type) {
          case 0xaa: {
            if (to === BUS.DSP) { const s = new Array(129).fill(0); s[0] = 0x80; s[61] = 87; reply(to, 0xaa, s); }
            else if (to === BUS.AVR) { const s = new Array(25).fill(0); s[0] = 0x18; s[1] = seen.armed ? 1 : 0; reply(to, 0xaa, s); }
            else reply(to, 0xaa, new Array(17).fill(0));
            break;
          }
          case 0x48: reply(to, 0xc8, [0x02, 0x80, 0x0e]); break;
          case 0x67: reply(to, 0xe7, ascii(`Mevo+ ${to === BUS.PI ? 'Pi' : 'board'} FW 17.04`, 75)); break;
          case 0xfd: reply(to, 0xfd, ascii('PI-ID', 34)); break;
          case 0x21: reply(to, 0xa0, [68, ...new Array(68).fill(0)]); break;
          case 0xbe: reply(to, 0xbf, [6, 0, 0, p[3], 0, 0, 1]); break;
          case 0xd2: reply(to, 0x95, [2, 0x30, 0x52]); reply(to, 0xd3, new Array(175).fill(0)); break;
          case 0xd0: text(to, 'DSP: CameraParam Read PASS'); reply(to, 0x95, [2, 0x30, 0x50]); reply(to, 0xd1, new Array(242).fill(0)); break;
          case 0x23: reply(to, 0xa2, [16, 1, 0xe1, 3, 0xe8, 0, 12, 3, 0xe8, 0, 0x3d, 0, 0, 0, 0, 0, 0]); break;
          case 0x9b: reply(to, 0x9b, p); break;
          case 0x83: reply(to, 0x82, [0x33, ...new Array(51).fill(0)]); break;
          case 0xde: reply(to, 0xde, [53, ...ascii(p[1] === 8 ? 'FS M2-123456\u0000123456789' : 'FS M2-123456', 53)]); break;
          case 0xa5: seen.modes.push(p[2]); reply(to, 0xa5, p); break;
          case 0xb0:
            if (p[1] === 1) {
              seen.arms++;
              // Refuse the very first arm once, like the real device does right after configuring
              if (seen.arms === 1) { reply(to, 0x94, [2, 0x30, 0x30]); break; }
              reply(to, 0x95, [2, 0x30, 0x30]); seen.armed = true;
              setTimeout(() => { text(BUS.DSP, 'System State 6'); text(BUS.AVR, `ARMED DetectionMode=${seen.modes.at(-1)}`); }, 20);
            } else {
              reply(to, 0x95, [2, 0x30, 0x30]);
              if (seen.armed) { seen.armed = false; setTimeout(() => { text(BUS.AVR, 'ARMED CANCELLED'); reply(BUS.AVR, 0xb1, [2, 0, 0]); text(BUS.DSP, 'System State 5'); }, 10); }
            }
            break;
          case 0x69: seen.acks++; if (seen.acks % 2 === 0) setTimeout(() => { reply(BUS.AVR, 0xb1, [2, 0, 0]); reply(BUS.AVR, 0xe5, ascii('\u0005IDLE', 6)); }, 30); break;
          case 0x6d: reply(BUS.AVR, 0xed, clubPayload()); break;
          default: break;
        }
      }
    });
    seen.fireShot = () => {
      seen.armed = false;
      reply(BUS.AVR, 0xe5, ascii('BALL TRIGGER: 10 ms back', 26));
      setTimeout(() => {
        reply(BUS.AVR, 0xd4, flightPayload());
        reply(BUS.AVR, 0xed, clubPayload());
        reply(BUS.AVR, 0xef, spinPayload());
        reply(BUS.AVR, 0xe5, ascii('\nPROCESSED', 11));
      }, 50);
    };
  });
  return { server, seen };
}


if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const port = +(process.argv[2] || 5100), every = +(process.argv[3] || 15) * 1000;
  const { server, seen } = fakeMevo();
  server.listen(port, () => console.log(`Fake Mevo+ on port ${port}; a shot every ${every / 1000} s once armed`));
  setInterval(() => { if (seen.armed && seen.fireShot) { console.log('shot!'); seen.fireShot(); } }, every);
}
