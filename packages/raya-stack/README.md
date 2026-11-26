# @ijoolaie/stack

The integrated distribution package for Raya MCP Stack.

## Component model

- `@ijoolaie/mcp-core@0.1.0` — MCP protocol runtime and client/server transport primitives.
- `@ijoolaie/local-mcp@0.1.0` — local machine/workspace tools; depends on Core.
- `@ijoolaie/bridge@0.1.0` — MCP endpoint connector and Admin Panel; depends on Core, not Local MCP.

Installing `@ijoolaie/stack` installs the complete three-component distribution. To select a smaller profile, install the corresponding component package directly; npm resolves Core automatically where required.

| Profile | Install command | Resolved components |
|---|---|---|
| Core only | `npm install @ijoolaie/mcp-core` | Core |
| Local MCP only | `npm install @ijoolaie/local-mcp` | Core + Local MCP |
| Bridge only | `npm install @ijoolaie/bridge` | Core + Bridge |
| Full Stack | `npm install @ijoolaie/stack` | Core + Local MCP + Bridge |

## Manifest API

```js
import { getStackProfile, listStackProfiles } from "@ijoolaie/stack";

console.log(listStackProfiles());
console.log(getStackProfile("bridge-only"));
```

The versioned manifest is also available from `@ijoolaie/stack/manifest`. It records exact component versions and profile selections. The current manifest is a local compatibility declaration, not a claim that packages have been published to a registry.

## Current limitations

- Registry publication and automated release promotion are not configured.
- Stack 0.1.0 pins the current 0.1.0 component set; compatibility policy and changelog/deprecation policy remain to be finalized.
- Tunnel Client remains an external optional integration for the Bridge Admin Panel.
