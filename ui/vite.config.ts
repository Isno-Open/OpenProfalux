import { defineConfig, type Plugin } from 'vite';
import vue from '@vitejs/plugin-vue';
import { copyFileSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

// L'interface du boitier. La construction donne trois fichiers aux noms fixes, index.html, app.js et
// style.css, que le firmware compresse et embarque (firmware/main/CMakeLists.txt) et sert sous ces memes
// noms (web_ui.c). Un seul fichier JavaScript : pas de morceaux charges a la demande, pas de route en plus.
//
//   npm run build                    les vraies routes du boitier (ce qu'embarque le firmware)
//   BOX=192.168.1.50 npm run dev     developpement contre un vrai boitier, l'API relayee vers lui
//   npm run dev:mock                 developpement sans carte, sur de fausses donnees (src/api/mock*.ts)

const MOCK_MODE = 'mock';
// Les fausses donnees (src/api/mock*.ts) : declarees sans effet de bord, elles disparaissent en entier de la vraie
// interface, sans meme leurs tableaux et lectures de haut de fichier.
const MOCK_FILES = /\/src\/api\/mock[^/]*\.ts$/;
const WEB_DIR = fileURLToPath(new URL('../firmware/main/web/', import.meta.url));
// Fichiers que le firmware sert lui-meme, a cote de l'interface : le serveur de developpement et la
// construction sur fausses donnees les reprennent de firmware/main/web (source unique).
const BOX_FILES: Record<string, string> = {
  '/manifest.webmanifest': 'application/manifest+json',
  '/icon-192.png': 'image/png',
  '/profiles.json': 'application/json',
};

function boxFiles(mock: boolean): Plugin {
  return {
    name: 'box-files',
    configureServer(server) {
      if (process.env.BOX) return;   // relayes au boitier avec l'API
      server.middlewares.use((req, res, next) => {
        const type = req.url ? BOX_FILES[req.url] : undefined;
        if (!type || !req.url) return next();
        res.setHeader('Content-Type', type);
        res.end(readFileSync(WEB_DIR + req.url.slice(1)));
      });
    },
    writeBundle(options) {
      if (!mock) return;   // le firmware sert les siens
      for (const file of Object.keys(BOX_FILES)) copyFileSync(WEB_DIR + file.slice(1), `${options.dir}${file}`);
    },
  };
}

export default defineConfig(({ mode }) => {
  const mock = mode === MOCK_MODE;
  const box = process.env.BOX;
  return {
    base: '/',
    publicDir: false,
    plugins: [vue(), boxFiles(mock)],
    /* Fausses donnees ou vraies routes, fixe a la construction : la branche ecartee (et les fausses donnees avec
       elle) disparait de app.js. Voir src/api/index.ts. */
    define: { __MOCK_DATA__: JSON.stringify(mock) },
    /* La detection de profil reste ou son banc node la teste (firmware/main/web), declaree dans src/lib. */
    resolve: { alias: { 'pyramid-detect': `${WEB_DIR}pyramid-detect.js` } },
    server: {
      host: '0.0.0.0',
      fs: { allow: ['..'] },   // profiles.json est lu dans firmware/main/web
      proxy: box ? Object.fromEntries(['/api', ...Object.keys(BOX_FILES)].map(path => [path, { target: `http://${box}`, changeOrigin: true }])) : undefined,
    },
    build: {
      outDir: mock ? 'dist-mock' : 'dist',
      emptyOutDir: true,
      assetsInlineLimit: 0,
      cssCodeSplit: false,
      modulePreload: false,
      rolldownOptions: {
        /* undefined : les autres modules gardent leur declaration (package.json « sideEffects » de Vue, notamment). */
        treeshake: { moduleSideEffects: (id: string): false | undefined => (MOCK_FILES.test(id) ? false : undefined) },
        output: {
          /* Avis des composants tiers embarques (licence MIT), poses en tete de app.js APRES la minification (qui
             effacerait un commentaire pose avant). Detail : NOTICE.md. */
          postBanner: '/*! OpenProfalux, interface du boitier. Embarque : Vue 3.5.44, (c) 2018-present Yuxi (Evan) You and Vue contributors ; '
            + 'qrcode-generator 2.0.4 (QR Code Generator for JavaScript), (c) 2009 Kazuhiko Arase. Licence MIT pour les deux : '
            + 'https://opensource.org/licenses/MIT. « QR Code » est une marque deposee de DENSO WAVE INCORPORATED. */',
          entryFileNames: 'app.js',
          codeSplitting: false,
          assetFileNames: info => (info.names && info.names.some(name => name.endsWith('.css')) ? 'style.css' : '[name][extname]'),
        },
      },
    },
  };
});
