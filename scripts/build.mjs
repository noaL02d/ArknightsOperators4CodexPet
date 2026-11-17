import { build, context } from 'esbuild';
import { cp, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const out = path.join(root, 'dist');
const watch = process.argv.includes('--watch');

const common = {
  bundle: true,
  sourcemap: true,
  logLevel: 'info',
  target: 'es2022',
  define: { 'process.env.NODE_ENV': JSON.stringify(process.env.NODE_ENV ?? 'development') },
};

async function copyStatic() {
  await mkdir(out, { recursive: true });
  await cp(path.join(root, 'src/renderer'), path.join(out, 'renderer'), { recursive: true });
  await cp(path.join(root, 'public'), path.join(out, 'public'), { recursive: true });
  // Drop TypeScript sources that are only useful to esbuild.
  for (const name of ['pet.ts', 'menu.ts', 'card.ts', 'exporter.ts']) {
    const p = path.join(out, 'renderer', name);
    if (existsSync(p)) await rm(p, { force: true });
  }
}

async function main() {
  if (!watch) await rm(out, { recursive: true, force: true });
  await mkdir(path.join(out, 'main'), { recursive: true });
  await mkdir(path.join(out, 'preload'), { recursive: true });
  await mkdir(path.join(out, 'renderer'), { recursive: true });

  const mainOptions = {
    ...common,
    entryPoints: [path.join(root, 'src/main/index.ts')],
    outfile: path.join(out, 'main/index.cjs'),
    platform: 'node',
    format: 'cjs',
    external: ['electron'],
  };
  const preloadOptions = {
    ...common,
    entryPoints: [path.join(root, 'src/preload/index.ts')],
    outfile: path.join(out, 'preload/index.cjs'),
    platform: 'node',
    format: 'cjs',
    external: ['electron'],
  };
  const rendererOptions = {
    ...common,
    entryPoints: {
      pet: path.join(root, 'src/renderer/pet.ts'),
      menu: path.join(root, 'src/renderer/menu.ts'),
      card: path.join(root, 'src/renderer/card.ts'),
      exporter: path.join(root, 'src/renderer/exporter.ts'),
    },
    outdir: path.join(out, 'renderer'),
    platform: 'browser',
    format: 'iife',
  };

  if (watch) {
    const ctxs = await Promise.all([
      context(mainOptions),
      context(preloadOptions),
      context(rendererOptions),
    ]);
    await Promise.all(ctxs.map((c) => c.watch()));
    await copyStatic();
    console.log('watching...');
    return;
  }

  await Promise.all([build(mainOptions), build(preloadOptions), build(rendererOptions)]);
  await copyStatic();
  console.log('build complete');
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
