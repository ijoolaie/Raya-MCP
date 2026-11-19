const fs = require('node:fs');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, 'pipe-io-v2.c'), 'utf8');

const checks = [
  ['no per-operation thread fallback', !/\bCreateThread\s*\(/.test(source)],
  ['no ignored blocking completion result', !/\(void\)\s*GetOverlappedResult\s*\(/.test(source)],
  ['no unconditional cleanup immediately after blocking collection',
    !/GetOverlappedResult\s*\([^;]+,\s*TRUE\s*\)\s*;\s*free_pending\s*\(/s.test(source)],
  ['no cleanup near failed nonblocking completion collection',
    !/if\s*\(\s*!GetOverlappedResult\s*\([^;]+,\s*FALSE\s*\)\s*\)\s*\{[\s\S]{0,500}?free_pending\s*\(/.test(source)]
];

let failed = 0;
for (const [name, ok] of checks) {
  process.stdout.write((ok ? 'PASS: ' : 'BLOCKER: ') + name + '\n');
  if (!ok) failed++;
}
process.stdout.write('SUMMARY: ' + (checks.length - failed) + '/' + checks.length + ' source invariants satisfied\n');
process.exitCode = failed ? 1 : 0;
