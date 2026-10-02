// Golden SimConnect bytes from node-simconnect, an independent client used with MSFS 2020/2024.
// Regenerate: npm install node-simconnect && node tests/simconnect/oracle.js > golden.json
// (the expected bytes in msfs-sender/MsfsHudSender.Tests/SimConnectGolden.cs come from its output)
const net = require('net');
const sc = require(process.env.NODE_SIMCONNECT || 'node-simconnect');

function msg(recvId, body) {          // server -> client message
  const head = Buffer.alloc(12);
  head.writeUInt32LE(12 + body.length, 0);
  head.writeUInt32LE(4, 4);           // protocol
  head.writeUInt32LE(recvId, 8);
  return Buffer.concat([head, body]);
}
function u32s(...v) { const b = Buffer.alloc(4 * v.length); v.forEach((x, i) => b.writeUInt32LE(x >>> 0, 4 * i)); return b; }
function str(s, n) { const b = Buffer.alloc(n); b.write(s, 'latin1'); return b; }
function f64s(...v) { const b = Buffer.alloc(8 * v.length); v.forEach((x, i) => b.writeDoubleLE(x, 8 * i)); return b; }

const out = { client: [], server: [], events: [] };
const recvOpen = msg(2, Buffer.concat([str('KittyHawk', 256), u32s(11, 0, 62651, 3, 11, 0, 62651, 3, 0, 0)]));
const simData = msg(8, Buffer.concat([u32s(7, 1, 1, 0, 0, 0, 2), f64s(0.1, -0.25)]));
const exception = msg(1, u32s(7, 3, 2));
const quit = msg(3, Buffer.alloc(0));
out.server = { recvOpen: recvOpen.toString('hex'), simData: simData.toString('hex'), exception: exception.toString('hex'), quit: quit.toString('hex') };

const server = net.createServer(sock => {
  let buf = Buffer.alloc(0);
  sock.on('data', d => {
    buf = Buffer.concat([buf, d]);
    while (buf.length >= 4 && buf.length >= buf.readUInt32LE(0)) {
      const n = buf.readUInt32LE(0), pkt = buf.slice(0, n); buf = buf.slice(n);
      out.client.push(pkt.toString('hex'));
      const type = pkt.readUInt32LE(8) & 0xffff;
      if (type === 0x01) sock.write(recvOpen);
      if (type === 0x0e) { sock.write(Buffer.concat([simData, exception])); setTimeout(() => sock.write(quit), 50); }
    }
  });
});
server.listen(0, '127.0.0.1', async () => {
  const port = server.address().port;
  const { recvOpen: ro, handle } = await sc.open('MsfsHudSender', sc.Protocol.FSX_SP2, { host: '127.0.0.1', port });
  out.events.push({ open: ro.applicationName, simConnectVersionMajor: ro.simConnectVersionMajor });
  handle.on('simObjectData', d => out.events.push({ simObjectData: { requestID: d.requestID, defineID: d.defineID, defineCount: d.defineCount, values: [d.data.readFloat64(), d.data.readFloat64()] } }));
  handle.on('exception', e => out.events.push({ exception: { exception: e.exception, sendId: e.sendId, index: e.index } }));
  handle.on('quit', () => { out.events.push({ quit: true }); console.log(JSON.stringify(out, null, 1)); process.exit(0); });
  handle.addToDataDefinition(1, 'PLANE PITCH DEGREES', 'radians', sc.SimConnectDataType.FLOAT64);
  handle.addToDataDefinition(1, 'GENERAL ENG RPM:1', 'rpm', sc.SimConnectDataType.FLOAT64);
  handle.requestDataOnSimObject(7, 1, sc.SimConnectConstants.OBJECT_ID_USER, sc.SimConnectPeriod.ONCE);
});
setTimeout(() => { console.log('TIMEOUT', JSON.stringify(out)); process.exit(1); }, 5000);
