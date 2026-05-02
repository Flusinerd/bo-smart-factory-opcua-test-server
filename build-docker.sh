docker build --platform linux/amd64,linux/arm64 -t flusinerd/hs-bo-opcua-bridge:latest . -f Dockerfile.bridge
docker push flusinerd/hs-bo-opcua-bridge:latest