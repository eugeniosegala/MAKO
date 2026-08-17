export interface LocalDevelopmentBuildInfo {
  generatedAt: string;
  plugin: {
    commit: string;
    dirty: boolean;
    frontendDeployed: boolean;
    backendDeployed: boolean;
  };
  engine: {
    commit: string;
    dirty: boolean;
    layer64Sha256: string | null;
    layer32Sha256: string | null;
    flatpakArchiveSha256: string | null;
  } | null;
}
