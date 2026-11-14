# Image du plugin OpenProfalux pour le catalogue d'OpenCloudApp, construite depuis la RACINE du depot
# (les types d'appareils viennent de firmware/main/web/profiles.json) :
#   git submodule update --init cloud-plugin/opencloudapp
#   docker build -f cloud-plugin/Dockerfile -t <registre>/openprofalux-cloud-plugin:<version> .
# Contrat de l'image (docs/PLUGINS.md d'OpenCloudApp) : /plugin/index.js, les modules qu'il importe et
# les contrats a cote, sans aucun import de paquet ; un shell pour la copie. Rien ne s'y execute.
FROM node:26.13.1-trixie-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends git ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY firmware/main/web/profiles.json ./firmware/main/web/profiles.json
COPY cloud-plugin/package.json cloud-plugin/package-lock.json ./cloud-plugin/
RUN cd cloud-plugin && npm ci --no-audit --no-fund
COPY cloud-plugin ./cloud-plugin
# les tests passent avant toute image : un plugin qui lit mal un boitier ne part pas
RUN cd cloud-plugin && npm test \
  && mkdir -p /out && cp package.json /out/package.json \
  && cp dist/src/index.js dist/src/ids.js dist/src/kinds.generated.js /out/ \
  && cp dist/opencloudapp/server/src/family-contract.js dist/opencloudapp/server/src/plugin-contract.js /out/ \
  && cp node_modules/@isno-open/plugin-host/dist/contract.js /out/contract.js \
  && sed -i 's#"../opencloudapp/server/src/family-contract.js"#"./family-contract.js"#; s#"../opencloudapp/server/src/plugin-contract.js"#"./plugin-contract.js"#' /out/index.js \
  && sed -i 's#"@isno-open/plugin-host/contract"#"./contract.js"#' /out/plugin-contract.js \
  && ! grep -nE "from \"[^.]" /out/*.js

FROM busybox:1.38.0-musl
COPY --from=build /out /plugin
