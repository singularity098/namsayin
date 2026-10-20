#!/usr/bin/env bash
# Fetches only the submodules the build uses, with retries. Eigen (inside NAM Core) is hosted on GitLab, which
# sometimes refuses requests under load; NAM Core's AudioDSPTools (with a second Eigen) isn't needed, so it's skipped.
set -u

fetch() {
    git submodule update --init --depth 1 &&
    git -C deps/DPF submodule update --init --recursive --depth 1 &&
    git -C deps/NeuralAmpModelerCore submodule update --init --depth 1 Dependencies/eigen
}

for attempt in 1 2 3 4 5 6; do
    fetch && exit 0
    echo "Submodule fetch failed (attempt $attempt), retrying in 60 s"
    sleep 60
done
exit 1
