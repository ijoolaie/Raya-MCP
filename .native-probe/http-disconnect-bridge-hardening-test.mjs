import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import { Readable } from "node:stream";
const MAX_BODY = 1024;
async function readSingleJsonRpc(req) {
  const chunks = []; let total = 0;
  for await (const chunk of req) { total += chunk.length; if (total > MAX_BODY) throw Object.assign(new Error("request body too large"), { status: 413 }); chunks.push(chunk); }
  let parsed;
  try { parsed = JSON.parse(Buffer.concat(chunks).toString("utf8")); } catch { throw Object.assign(new Error("malformed JSON"), { status: 400 }); }
  if (Array.isArray(parsed)) throw Object.assign(new Error("batch requests are not supported"), { status: 400 });
  if (!parsed || typeof parsed !== "object" || parsed.jsonrpc !== "2.0" || typeof parsed.method !== "string") throw Object.assign(new Error("invalid JSON-RPC request"), { status: 400 });
  return parsed;
}
function requestKey(sessionId, id) {
  if (id === undefined || id === null) throw new Error("request id required");
  return String(sessionId || "__initialization__") + "::" + JSON.stringify(id);
}
function attachDisconnect(res, controllers) {
  res.once("close", () => { if (!res.writableEnded) for (const controller of controllers.values()) controller.abort(new Error("HTTP response disconnected")); });
}
function registerRequest(map, session, id) {
  const key = requestKey(session, id); const controller = new AbortController();
  if (map.has(key)) throw new Error("duplicate active request id");
  map.set(key, controller); return { key, controller };
}
function cleanup(map, key, controller) { if (map.get(key) === controller) map.delete(key); }
async function main() {
  let checks = 0;
  const good = Buffer.from(JSON.stringify({ jsonrpc: "2.0", id: 7, method: "tools/call" }));
  const parsed = await readSingleJsonRpc(Readable.from([good.subarray(0, 9), good.subarray(9)]));
  assert.equal(parsed.id, 7); checks++;
  await assert.rejects(readSingleJsonRpc(Readable.from([Buffer.from("{broken")])), e => e.status === 400); checks++;
  await assert.rejects(readSingleJsonRpc(Readable.from([Buffer.alloc(MAX_BODY + 1, 0x61)])), e => e.status === 413); checks++;
  await assert.rejects(readSingleJsonRpc(Readable.from([Buffer.from(JSON.stringify([JSON.parse(good)]))])), e => e.status === 400 && /batch/.test(e.message)); checks++;
  await assert.rejects(readSingleJsonRpc(Readable.from([Buffer.from(JSON.stringify({ jsonrpc: "1.0", method: "x" }))])), e => e.status === 400); checks++;
  const registry = new Map();
  const a = registerRequest(registry, "session-A", 11); const b = registerRequest(registry, "session-B", 11);
  assert.notEqual(a.key, b.key); assert.equal(registry.size, 2); checks++;
  const numeric = registerRequest(registry, "session-C", 11); const string = registerRequest(registry, "session-C", "11");
  assert.notEqual(numeric.key, string.key); checks++;
  await assert.rejects(Promise.resolve().then(() => registerRequest(registry, "session-A", 11)), /duplicate/); checks++;
  const normalRes = new EventEmitter(); normalRes.writableEnded = true;
  const normal = registerRequest(registry, "session-D", 1); attachDisconnect(normalRes, new Map([[normal.key, normal.controller]])); normalRes.emit("close");
  assert.equal(normal.controller.signal.aborted, false); checks++;
  const droppedRes = new EventEmitter(); droppedRes.writableEnded = false;
  const dropped = registerRequest(registry, "session-E", 1); attachDisconnect(droppedRes, new Map([[dropped.key, dropped.controller]])); droppedRes.emit("close");
  assert.equal(dropped.controller.signal.aborted, true); checks++;
  cleanup(registry, a.key, a.controller); assert.equal(registry.has(a.key), false);
  cleanup(registry, b.key, a.controller); assert.equal(registry.get(b.key), b.controller); checks++;
  const interrupted = new Readable({ read() { this.push(Buffer.from('{"jsonrpc":"2.0"')); this.destroy(new Error("client disconnected during upload")); } });
  await assert.rejects(readSingleJsonRpc(interrupted)); assert.equal(registry.size, 5); checks++;
  for (const entry of [b, numeric, string, normal, dropped]) cleanup(registry, entry.key, entry.controller);
  assert.equal(registry.size, 0); checks++;
  process.stdout.write(JSON.stringify({ ok: true, checks, bodyLimit: MAX_BODY, batchPolicy: "explicitly rejected", sessionAndIdIsolation: true, normalCloseSafe: true, prematureCloseAborts: true, cleanup: "identity-checked and complete" }) + "\n");
}
main().catch(error => { console.error(error); process.exitCode = 1; });
