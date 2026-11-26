# Private package publishing — GitHub Packages
Four independently versioned npm packages use the repository owner's scope:
- @ijoolaie/mcp-core
- @ijoolaie/local-mcp
- @ijoolaie/bridge
- @ijoolaie/stack

Registry: https://npm.pkg.github.com
Each package manifest declares publishConfig.registry and links to https://github.com/ijoolaie/Raya-MCP. The package scope matches the GitHub owner ijoolaie; @raya/* is not used because that scope does not match this repository owner.

## Automated publishing
Workflow: .github/workflows/publish-github-packages.yml
- Trigger: push a tag named packages-v<stack-version> or manually dispatch the workflow.
- Requires the tag version to match packages/raya-stack/package.json.
- Runs npm ci and npm run verify:modular before publishing.
- Publishes Core, Local MCP, Bridge, then Stack.
- Uses GITHUB_TOKEN with contents: read and packages: write; no long-lived PAT is stored.
- Does not create tags, releases, commits, or push changes.

## First-time repository setup
1. Commit and push the prepared changes to ijoolaie/Raya-MCP (not performed by this staging task).
2. Ensure GitHub Actions are enabled and permitted to create packages.
3. After the first publication, confirm each package is private and access is limited to intended collaborators.
4. If prompted, link each package to ijoolaie/Raya-MCP.
5. Create and push packages-v<version> only after release review. Manual dispatch also publishes and must be treated as a release action.

## Installing from a local machine
Configure npm for the @ijoolaie scope:
    @ijoolaie:registry=https://npm.pkg.github.com
    //npm.pkg.github.com/:_authToken=\${GITHUB_PACKAGES_TOKEN}
    always-auth=true

Set GITHUB_PACKAGES_TOKEN outside source control to a classic personal access token with read:packages and access to the private repository/package. Never commit the token or a populated .npmrc.
    npm install @ijoolaie/mcp-core
    npm install @ijoolaie/local-mcp
    npm install @ijoolaie/bridge
    npm install @ijoolaie/stack

## Important release behavior
GitHub Packages has no dry-run that proves remote authorization or package creation without publishing. Local npm pack --dry-run and modular clean-install tests validate package contents and dependencies, but first remote publication is a separate deliberate action. Package versions are immutable in normal use; increment versions before retrying an already-published version.