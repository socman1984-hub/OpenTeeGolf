// Pretends to be a launch monitor: connects to the OpenConnect port and sends one shot,
// exactly like the FS Golf app does for a Mevo+.
// Usage: node test/send-shot.js [speedMph] [vla] [hla] [totalSpin] [spinAxis] [host]
import net from 'node:net';

const [speed = 150, vla = 12, hla = 0, spin = 2700, axis = 0] = process.argv.slice(2, 7).map(Number);
const host = process.argv[7] || '127.0.0.1';
const port = +(process.env.OPENCONNECT_PORT || 921);

const shot = {
  DeviceID: 'Mevo+ (simulated)',
  Units: 'Yards',
  ShotNumber: Math.floor(Math.random() * 1e6),
  APIversion: '1',
  BallData: { Speed: speed, SpinAxis: axis, TotalSpin: spin, HLA: hla, VLA: vla },
  ShotDataOptions: { ContainsBallData: true, ContainsClubData: false, LaunchMonitorIsReady: true, LaunchMonitorBallDetected: true, IsHeartBeat: false },
};

const sock = net.connect(port, host, () => {
  console.log(`Connected to ${host}:${port}`);
  // Heartbeat first, then the shot, mimicking a real connector
  sock.write(JSON.stringify({ ...shot, BallData: undefined, ShotDataOptions: { ...shot.ShotDataOptions, ContainsBallData: false, IsHeartBeat: true } }));
  setTimeout(() => sock.write(JSON.stringify(shot)), 300);
});
sock.setEncoding('utf8');
sock.on('data', (d) => console.log('<-', d));
sock.on('error', (e) => { console.error('Connection failed:', e.message); process.exit(1); });
setTimeout(() => sock.end(), 1500);
