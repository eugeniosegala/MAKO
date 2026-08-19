declare module "virtual:mako-release-info" {
  export const currentRelease: Readonly<{
    version: string;
    codename: string;
  }>;
}
