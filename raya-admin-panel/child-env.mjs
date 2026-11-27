const WINDOWS_RUNTIME_ENV_KEYS = Object.freeze([
  "PATH",
  "SystemRoot",
  "WINDIR",
  "TEMP",
  "TMP",
  "USERPROFILE",
  "APPDATA",
  "LOCALAPPDATA",
  "ProgramFiles",
  "ProgramFiles(x86)",
  "ProgramData",
  "ComSpec",
  "PROCESSOR_ARCHITECTURE",
  "PATHEXT"
]);

/**
 * Build the minimal environment required by the local MCP child process.
 * Secret-bearing and execution-injection variables are intentionally excluded.
 */
export function buildChildEnvironment(sourceEnv, { allowedRoots, powershellEnabled }) {
  if (!sourceEnv || typeof sourceEnv !== "object" || Array.isArray(sourceEnv)) {
    throw new TypeError("sourceEnv must be an environment object.");
  }
  if (typeof allowedRoots !== "string" || !allowedRoots.trim()) {
    throw new TypeError("allowedRoots must be a non-empty string.");
  }

  const env = {};
  for (const key of WINDOWS_RUNTIME_ENV_KEYS) {
    if (typeof sourceEnv[key] === "string") env[key] = sourceEnv[key];
  }

  env.MCP_ALLOWED_ROOTS = allowedRoots;
  env.MCP_ENABLE_POWERSHELL = powershellEnabled === true ? "true" : "false";
  return env;
}
