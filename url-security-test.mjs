import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import https from "node:https";
import { registerUrlTools, resolveHost } from "./url-tools.mjs";

function makeHandler(queue, requests) {
  let handler;
  const fakeServer = {
    tool(name, description, schema, fn) {
      assert.equal(name, "read_url");
      handler = fn;
    }
  };
  registerUrlTools(fakeServer);
  const originalRequest = https.request;
  https.request = (options, callback) => {
    requests.push({ hostname: options.hostname, path: options.path });
    const req = new EventEmitter();
    req.end = () => {
      const spec = queue.shift();
      if (!spec) throw new Error("Mock response queue exhausted.");
      const res = new EventEmitter();
      res.statusCode = spec.status;
      res.headers = spec.headers || {};
      setImmediate(() => {
        callback(res);
        if (spec.body) res.emit("data", Buffer.from(spec.body));
        res.emit("end");
      });
    };
    req.destroy = error => {
      if (error) req.emit("error", error);
    };
    return req;
  };
  return {
    handler,
    restore() { https.request = originalRequest; }
  };
}

{
  const requests = [];
  const mock = makeHandler([
    { status: 200, headers: { "content-type": "text/plain", "content-length": "6" }, body: "public" }
  ], requests);
  try {
    const result = await mock.handler({ url: "https://[2606:4700:4700::1111]/public", maxBytes: 1024 });
    const value = JSON.parse(result.content[0].text);
    assert.equal(value.status, 200);
    assert.equal(value.text, "public");
    assert.equal(requests.length, 1, "public IPv6 should be allowed");
  } finally {
    mock.restore();
  }
}

{
  const requests = [];
  const mock = makeHandler([
    { status: 200, headers: { "content-type": "text/plain", "content-length": "6" }, body: "public" }
  ], requests);
  try {
    const result = await mock.handler({ url: "https://192.0.1.1/public", maxBytes: 1024 });
    const value = JSON.parse(result.content[0].text);
    assert.equal(value.status, 200);
    assert.equal(value.text, "public");
    assert.equal(requests.length, 1, "192.0.1.0/24 is outside the blocked 192.0.0.0/24 special-purpose prefix");
  } finally {
    mock.restore();
  }
}

for (const address of ["172.15.255.255", "172.32.0.0", "100.128.0.1"]) {
  const requests = [];
  const mock = makeHandler([
    { status: 200, headers: { "content-type": "text/plain", "content-length": "6" }, body: "public" }
  ], requests);
  try {
    const result = await mock.handler({ url: `https://${address}/public-boundary`, maxBytes: 1024 });
    const value = JSON.parse(result.content[0].text);
    assert.equal(value.status, 200, `${address} should be allowed outside private/shared prefixes`);
    assert.equal(value.text, "public");
    assert.equal(requests.length, 1);
  } finally {
    mock.restore();
  }
}

{
  const requests = [];
  const mock = makeHandler([
    { status: 302, headers: { location: "https://127.0.0.1/private" } }
  ], requests);
  try {
    await assert.rejects(
      mock.handler({ url: "https://1.1.1.1/start", maxBytes: 1024 }),
      /non-public IP address/
    );
    assert.equal(requests.length, 1, "private redirect must be blocked before a second request");
    assert.equal(requests[0].hostname, "1.1.1.1");
  } finally {
    mock.restore();
  }
}

{
  const requests = [];
  const mock = makeHandler([
    { status: 302, headers: { location: "https://8.8.8.8/final" } },
    { status: 200, headers: { "content-type": "text/plain", "content-length": "4" }, body: "safe" }
  ], requests);
  try {
    const result = await mock.handler({ url: "https://1.1.1.1/start", maxBytes: 1024 });
    const value = JSON.parse(result.content[0].text);
    assert.equal(value.status, 200);
    assert.equal(value.text, "safe");
    assert.deepEqual(requests.map(x => x.hostname), ["1.1.1.1", "8.8.8.8"]);
  } finally {
    mock.restore();
  }
}

{
  const requests = [];
  const mock = makeHandler(Array.from({ length: 5 }, () => ({
    status: 302, headers: { location: "https://8.8.8.8/loop" }
  })), requests);
  try {
    await assert.rejects(
      mock.handler({ url: "https://1.1.1.1/start", maxBytes: 1024 }),
      /Too many redirects/
    );
    assert.equal(requests.length, 4, "redirect loop must stop after the configured hop limit");
  } finally {
    mock.restore();
  }
}

for (const testCase of [
  { url: "http://1.1.1.1/plain", pattern: /Only HTTPS URLs are allowed/, label: "HTTP scheme rejected" },
  { url: "https://user:pass@1.1.1.1/secret", pattern: /Credential-bearing URLs are blocked/, label: "credential-bearing URL rejected" },
  { url: "https://1.1.1.1:8443/nonstandard", pattern: /Only the standard HTTPS port is allowed/, label: "non-standard HTTPS port rejected" },
  { url: "https://[::1]/loopback", pattern: /non-public IP address/, label: "IPv6 loopback rejected" },
  { url: "https://[fc00::1]/private", pattern: /non-public IP address/, label: "IPv6 unique-local address rejected" },
  { url: "https://[2001:2::1]/benchmark", pattern: /non-public IP address/, label: "IPv6 benchmarking range rejected" },
  { url: "https://0.0.0.0/unspecified", pattern: /non-public IP address/, label: "IPv4 unspecified range rejected" },
  { url: "https://100.64.0.1/shared", pattern: /non-public IP address/, label: "IPv4 shared address space rejected" },
  { url: "https://100.127.255.255/shared-boundary", pattern: /non-public IP address/, label: "upper boundary of IPv4 shared address space rejected" },
  { url: "https://172.16.0.0/private-boundary", pattern: /non-public IP address/, label: "lower boundary of IPv4 private 172.16/12 rejected" },
  { url: "https://172.31.255.255/private-boundary", pattern: /non-public IP address/, label: "upper boundary of IPv4 private 172.16/12 rejected" },
  { url: "https://192.0.2.1/documentation", pattern: /non-public IP address/, label: "IPv4 documentation range rejected" },
  { url: "https://198.18.0.1/benchmark", pattern: /non-public IP address/, label: "IPv4 benchmarking range rejected" },
  { url: "https://198.51.100.1/documentation", pattern: /non-public IP address/, label: "IPv4 TEST-NET-2 rejected" },
  { url: "https://203.0.113.1/documentation", pattern: /non-public IP address/, label: "IPv4 TEST-NET-3 rejected" },
  { url: "https://224.0.0.1/multicast", pattern: /non-public IP address/, label: "IPv4 multicast range rejected" },
  { url: "https://240.0.0.1/reserved", pattern: /non-public IP address/, label: "IPv4 reserved range rejected" },
  { url: "https://[fe80::1]/link-local", pattern: /non-public IP address/, label: "IPv6 link-local rejected" },
  { url: "https://[ff02::1]/multicast", pattern: /non-public IP address/, label: "IPv6 multicast rejected" },
  { url: "https://[2001:db8::1]/documentation", pattern: /non-public IP address/, label: "IPv6 documentation range rejected" },
  { url: "https://[2001:3::1]/protocol-assignment", pattern: /non-public IP address/, label: "IPv6 protocol-assignment range rejected" },
  { url: "https://[2001:4:112::1]/as112", pattern: /non-public IP address/, label: "IPv6 AS112 range rejected" },
  { url: "https://[2001:100::1]/discard-only", pattern: /non-public IP address/, label: "IPv6 discard-only prefix rejected" },
  { url: "https://[2001:0100:0000:0000:0000:0000:0000:0001]/expanded-discard-only", pattern: /non-public IP address/, label: "IPv6 expanded discard-only prefix rejected" },
  { url: "https://[2002::1]/6to4", pattern: /non-public IP address/, label: "IPv6 6to4 range rejected" },
  { url: "https://[::ffff:127.0.0.1]/mapped-loopback", pattern: /non-public IP address/, label: "IPv4-mapped IPv6 loopback rejected" },
  { url: "https://[0:0:0:0:0:ffff:7f00:1]/expanded-mapped-loopback", pattern: /non-public IP address/, label: "expanded IPv4-mapped IPv6 loopback rejected" }
]) {
  const requests = [];
  const mock = makeHandler([], requests);
  try {
    await assert.rejects(
      mock.handler({ url: testCase.url, maxBytes: 1024 }),
      testCase.pattern,
      testCase.label
    );
    assert.equal(requests.length, 0, `${testCase.label}: no network request must be made`);
  } finally {
    mock.restore();
  }
}

await assert.rejects(
  resolveHost("timeout.invalid", () => new Promise(() => {}), 10),
  /DNS lookup timed out/,
  "DNS lookup timeout is enforced"
);

console.log(JSON.stringify({
  ok: true,
  checked: 36,
  checks: [
    "public IPv6 address allowed",
    "public IPv4 address in 192.0.1.0/24 allowed",
    "IPv4 addresses immediately outside private/shared ranges allowed",
    "IPv4 shared-range upper boundary rejected",
    "IPv4 172.16/12 lower boundary rejected",
    "IPv4 172.16/12 upper boundary rejected",
    "private redirect blocked before follow-up request",
    "public redirect revalidated and followed",
    "redirect hop limit enforced",
    "HTTP scheme rejected",
    "credential-bearing URL rejected",
    "non-standard HTTPS port rejected",
    "IPv6 loopback rejected",
    "IPv6 unique-local address rejected",
    "IPv6 benchmarking range rejected",
    "IPv4 unspecified range rejected",
    "IPv4 shared address space rejected",
    "IPv4 documentation range rejected",
    "IPv4 benchmarking range rejected",
    "IPv4 TEST-NET-2 rejected",
    "IPv4 TEST-NET-3 rejected",
    "IPv4 multicast range rejected",
    "IPv4 reserved range rejected",
    "IPv6 link-local rejected",
    "IPv6 multicast rejected",
    "IPv6 documentation range rejected",
    "IPv6 protocol-assignment range rejected",
    "IPv6 AS112 range rejected",
    "IPv6 discard-only prefix rejected",
    "IPv6 expanded discard-only prefix rejected",
    "IPv6 6to4 range rejected",
    "IPv4-mapped IPv6 loopback rejected",
    "expanded IPv4-mapped IPv6 loopback rejected",
    "DNS lookup timeout enforced"
  ]
}, null, 2));
