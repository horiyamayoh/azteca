#!/usr/bin/env node

import { existsSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const script_dir = dirname(fileURLToPath(import.meta.url));
const repo_root = join(script_dir, "..");
const target = "docs/reference/azteca_design_all_in_one_v3.md";

const static_files = ["docs/README.md"];
const generated_directories = ["docs/design", "docs/planning", "docs/review", "docs/adr"];

function markdown_files_in(directory) {
  const absolute_directory = join(repo_root, directory);

  return readdirSync(absolute_directory, { withFileTypes: true })
    .filter((entry) => entry.isFile() && entry.name.endsWith(".md"))
    .map((entry) => `${directory}/${entry.name}`)
    .sort((left, right) => left.localeCompare(right));
}

function source_files() {
  return [
    ...static_files,
    ...generated_directories.flatMap((directory) => markdown_files_in(directory))
  ];
}

function normalized_content(relative_path) {
  const content = readFileSync(join(repo_root, relative_path), "utf8");
  return content.replace(/[ \t]+\n/gu, "\n").replace(/\n*$/u, "\n");
}

function generate_reference() {
  const header = [
    "# Azteca Design Documents V3 - All in One",
    "",
    "This file is generated from `docs/README.md`, `docs/design/`, `docs/planning/`,",
    "`docs/review/`, and `docs/adr/`.",
    "",
    "`docs/development.md` is intentionally excluded because it is operational developer",
    "guidance, not design source of truth.",
    "",
    "Run `npm run docs:reference` after editing design documents."
  ].join("\n");

  const sections = source_files().map((relative_path) => {
    return `---\n\n# File: ${relative_path}\n\n${normalized_content(relative_path)}`;
  });

  return `${header}\n\n${sections.join("\n")}`;
}

const generated_reference = generate_reference();
const target_path = join(repo_root, target);
const check_mode = process.argv.includes("--check");

if (check_mode) {
  if (!existsSync(target_path)) {
    console.error(`${target} is missing. Run \`npm run docs:reference\`.`);
    process.exit(1);
  }

  const existing_reference = readFileSync(target_path, "utf8");

  if (existing_reference !== generated_reference) {
    console.error(`${target} is out of date. Run \`npm run docs:reference\`.`);
    process.exit(1);
  }

  console.log(`${target} is up to date.`);
} else {
  writeFileSync(target_path, generated_reference, "utf8");
  console.log(`Wrote ${target}.`);
}
