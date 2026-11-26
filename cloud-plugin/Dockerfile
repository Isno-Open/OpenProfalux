# Image du plugin OpenProfalux pour le catalogue d'OpenCloudApp, construite depuis la RACINE du depot
# (les types d'appareils viennent de firmware/main/web/profiles.json, leurs icones de ui/src/assets/icons.svg.txt) :
#   git submodule update --init cloud-plugin/opencloudapp
#   docker build -f cloud-plugin/Dockerfile -t <registre>/openprofalux-cloud-plugin:<version> .
# Contrat de l'image (docs/PLUGINS.md d'OpenCloudApp) : /plugin/index.js, les modules qu'il importe et
# les contrats a cote, sans aucun import de paquet ; un shell pour la copie. Rien ne s'y execute.
FROM node:26.13.1-trixie-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends git ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY firmware/main/web/profiles.json ./firmware/main/web/profiles.json
# les icones des types sont celles de la planche du boitier
COPY ui/src/assets/icons.svg.txt ./ui/src/assets/icons.svg.txt
COPY cloud-plugin/package.json cloud-plugin/package-lock.json ./cloud-plugin/
RUN cd cloud-plugin && npm ci --no-audit --no-fund
COPY cloud-plugin ./cloud-plugin
# les tests passent avant toute image : un plugin qui lit mal un boitier ne part pas ; tous ses modules
# partent dans l'image, et chaque import relatif doit y trouver son fichier (oubli constate le 2026-11-15 :
# remotes.js absent, plugin refuse au chargement)
RUN cd cloud-plugin && npm test \
  && mkdir -p /out && cp package.json /out/package.json \
  && cp dist/src/*.js /out/ \
  && cp dist/opencloudapp/server/src/family-contract.js dist/opencloudapp/server/src/plugin-contract.js dist/opencloudapp/server/src/version.js /out/ \
  && cp node_modules/@isno-open/plugin-host/dist/contract.js /out/contract.js \
  && sed -i 's#"\.\./opencloudapp/server/src/\([a-z-]*\.js\)"#"./\1"#g; s#"@isno-open/plugin-host/contract"#"./contract.js"#g' /out/*.js \
  && ! grep -nE "from \"[^.]" /out/*.js \
  && node -e 'const fs=require("fs");let bad=0;for(const f of fs.readdirSync("/out").filter(x=>x.endsWith(".js")))for(const m of fs.readFileSync("/out/"+f,"utf8").matchAll(/from "\.\/([^"]+)"/g))if(!fs.existsSync("/out/"+m[1])){console.error(f+" importe "+m[1]+", absent de l image");bad=1}process.exit(bad)' 

FROM busybox:1.38.0-musl
COPY --from=build /out /plugin
