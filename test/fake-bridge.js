// Stands in for a launch monitor bridge program (tenover-frp, allsquare-frp) in tests: prints the
// same progress lines, serves FRP on the address it's given and fires one shot. With --fail it
// gives up as the real ones do when no device is found.
// Usage: node test/fake-bridge.js [--fail] 127.0.0.1:5881 [club]
import { fakeFrp } from './fake-frp.js';

const args = process.argv.slice(2);
const fail = args[0] === '--fail';
if (fail) args.shift();
const [addr, club = ''] = args;
const say = (m) => process.stderr.write(`tenover-frp: ${m}\n`);
say(`serving controllers on ${addr}`);
say('searching for Garmin R10...');
if (fail) {
  setTimeout(() => { say('connection failed: no R10 found'); process.exit(1); }, 200);
} else {
  const dev = fakeFrp({ port: +addr.split(':')[1], device: `Garmin R10 AA:BB:CC ${club}`.trim(), model: 'Approach R10', manufacturer: 'Garmin' });
  await dev.listen();
  setTimeout(() => {
    say('connected  AA:BB:CC  mtu=247');
    say('handshake complete');
    say('ready');
  }, 200);
  // one shot once a controller is listening
  const t = setInterval(() => {
    if (!dev.clients()) return;
    clearInterval(t);
    setTimeout(() => dev.shot({ ball: { launch_speed: '45mps', launch_elevation: 20, backspin_rpm: 7000, sidespin_rpm: 300 } }), 300);
  }, 50);
}
