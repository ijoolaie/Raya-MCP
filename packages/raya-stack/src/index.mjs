import fs from "node:fs";
import { fileURLToPath } from "node:url";

const manifestPath = new URL("../stack-manifest.json", import.meta.url);
const manifest = JSON.parse(fs.readFileSync(fileURLToPath(manifestPath), "utf8"));

export const RAYA_STACK_MANIFEST = Object.freeze(manifest);

export function listStackProfiles() {
  return Object.keys(manifest.profiles);
}

export function getStackProfile(name) {
  const profile = manifest.profiles[name];
  if (!profile) throw new RangeError("Unknown Raya MCP Stack profile: " + name);
  return {
    name,
    stackVersion: manifest.stackVersion,
    install: [...profile.install],
    description: profile.description
  };
}
