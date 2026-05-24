#!/usr/bin/env node
// Validates every Phase A inspect golden against the frozen JSON Schema.
// Exit 0 on success; non-zero with one error block per invalid file otherwise.

import { readFile, readdir, stat } from "node:fs/promises";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import Ajv2020 from "ajv/dist/2020.js";
import addFormats from "ajv-formats";

const __dirname = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(__dirname, "..");
const schemaPath = join(repoRoot, "tests/contract/schema_v2/azteca_phase_a.schema.json");
const goldenRoot = join(repoRoot, "tests/golden/phase_a");

async function walk(dir) {
	const out = [];
	for (const entry of await readdir(dir, { withFileTypes: true })) {
		const full = join(dir, entry.name);
		if (entry.isDirectory()) {
			out.push(...(await walk(full)));
		} else if (entry.isFile() && entry.name.endsWith(".inspect.json")) {
			out.push(full);
		}
	}
	return out;
}

async function main() {
	const schema = JSON.parse(await readFile(schemaPath, "utf8"));
	const ajv = new Ajv2020({ allErrors: true, strict: false });
	addFormats(ajv);
	const validate = ajv.compile(schema);

	try {
		await stat(goldenRoot);
	} catch {
		console.error(`golden directory missing: ${goldenRoot}`);
		process.exit(2);
	}

	const goldens = (await walk(goldenRoot)).sort();
	if (goldens.length === 0) {
		console.error(`no goldens found under ${goldenRoot}`);
		process.exit(2);
	}

	let failures = 0;
	for (const file of goldens) {
		const data = JSON.parse(await readFile(file, "utf8"));
		if (!validate(data)) {
			failures += 1;
			console.error(`FAIL ${relative(repoRoot, file)}`);
			for (const err of validate.errors ?? []) {
				console.error(`  ${err.instancePath || "/"} ${err.message}`);
			}
		}
	}

	if (failures > 0) {
		console.error(`\n${failures} of ${goldens.length} goldens failed schema validation`);
		process.exit(1);
	}
	console.log(`OK: ${goldens.length} goldens valid against schema_v2`);
}

main().catch((err) => {
	console.error(err);
	process.exit(2);
});
