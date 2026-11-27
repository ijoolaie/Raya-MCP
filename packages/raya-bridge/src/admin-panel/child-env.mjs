const WINDOWS_RUNTIME_ENV = [
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
  "PATHEXT",
  "HOMEDRIVE",
  "HOMEPATH",
  "USERNAME",
  "USERDOMAIN"
];

export function buildTunnelEnvironment(sourceEnv = process.env) {
  if (!sourceEnv || typeof sourceEnv !== "object" || Array.isArray(sourceEnv)) {
    throw new TypeError("sourceEnv must be an environment object.");
  }

  const childEnv = {};
  for (const key of WINDOWS_RUNTIME_ENV) {
    const value = sourceEnv[key];
    if (typeof value === "string" && value.length > 0) childEnv[key] = value;
  }

  // The tunnel client needs this credential to authenticate. No other
  // application credentials or Node.js runtime injection variables pass through.
  if (typeof sourceEnv.CONTROL_PLANE_API_KEY === "string" && sourceEnv.CONTROL_PLANE_API_KEY.length > 0) {
    childEnv.CONTROL_PLANE_API_KEY = sourceEnv.CONTROL_PLANE_API_KEY;
  }

  return childEnv;
}
