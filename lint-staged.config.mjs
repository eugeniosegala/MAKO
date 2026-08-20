export default {
  "*.md": "pnpm --dir plugin exec prettier --write --config ../.prettierrc.json --ignore-path ../.prettierignore",
};
