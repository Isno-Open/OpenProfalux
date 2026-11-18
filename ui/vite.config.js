import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';
import { copyFileSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

// L'interface du boitier. La construction donne trois fichiers aux noms fixes, index.html, app.js et
// style.css, que le firmware compresse et embarque (firmware/main/CMakeLists.txt) et sert sous ces memes
// noms (web_ui.c). Un seul fichier JavaScript : pas de morceaux charges a la demande, pas de route en plus.
//
//   npm run build            les vraies routes du boitier (ce qu'embarque le firmware)
//   BOX=192.168.1.50 npm run dev     developpement contre un vrai boitier, l'API relayee vers lui
//   npm run dev:maquette     developpement sans carte, sur de fausses donnees (src/api/mock*.js, .env.maquette)

const WEB = fileURLToPath(new URL('../firmware/main/web/', import.meta.url));
// Fichiers que le firmware sert lui-meme, a cote de l'interface : le serveur de developpement et la
// construction sur fausses donnees les reprennent de firmware/main/web (source unique).
const DU_BOITIER = { '/manifest.webmanifest': 'application/manifest+json', '/icon-192.png': 'image/png', '/profiles.json': 'application/json' };

function fichiersDuBoitier(maquette) {
  return {
    name: 'fichiers-du-boitier',
    configureServer(server) {
      if (process.env.BOX) return;   // relayes au boitier avec l'API
      server.middlewares.use((req, res, next) => {
        const type = DU_BOITIER[req.url];
        if (!type) return next();
        res.setHeader('Content-Type', type);
        res.end(readFileSync(WEB + req.url.slice(1)));
      });
    },
    writeBundle(options) {
      if (!maquette) return;   // le firmware sert les siens
      for (const f of Object.keys(DU_BOITIER)) copyFileSync(WEB + f.slice(1), `${options.dir}${f}`);
    },
  };
}

export default defineConfig(({ mode }) => {
  const maquette = mode === 'maquette';
  const box = process.env.BOX;
  return {
    base: '/',
    publicDir: false,
    plugins: [vue(), fichiersDuBoitier(maquette)],
    server: {
      host: '0.0.0.0',
      fs: { allow: ['..'] },   // profiles.json est lu dans firmware/main/web
      proxy: box ? Object.fromEntries(['/api', ...Object.keys(DU_BOITIER)].map(p => [p, { target: `http://${box}`, changeOrigin: true }])) : undefined,
    },
    build: {
      outDir: maquette ? 'dist-maquette' : 'dist',
      emptyOutDir: true,
      assetsInlineLimit: 0,
      cssCodeSplit: false,
      modulePreload: false,
      rollupOptions: {
        output: {
          /* Avis des composants tiers embarques (licence MIT), poses en tete de app.js APRES la minification (qui
             effacerait un commentaire pose avant). Detail : NOTICE.md. */
          postBanner: '/*! OpenProfalux, interface du boitier. Embarque : Vue 3.5.44, (c) 2018-present Yuxi (Evan) You and Vue contributors ; '
            + 'qrcode-generator 2.0.4 (QR Code Generator for JavaScript), (c) 2009 Kazuhiko Arase. Licence MIT pour les deux : '
            + 'https://opensource.org/licenses/MIT. « QR Code » est une marque deposee de DENSO WAVE INCORPORATED. */',
          entryFileNames: 'app.js',
          codeSplitting: false,
          assetFileNames: info => (info.names && info.names.some(n => n.endsWith('.css')) ? 'style.css' : '[name][extname]'),
        },
      },
    },
  };
});
