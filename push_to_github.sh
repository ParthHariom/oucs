#!/bin/zsh
# OUCS Engine — GitHub Push Script
# Run this after creating your repo on github.com/new
#
# Usage: ./push_to_github.sh YOUR_GITHUB_USERNAME

if [ -z "$1" ]; then
  echo "Usage: ./push_to_github.sh YOUR_GITHUB_USERNAME"
  exit 1
fi

USERNAME="$1"
REPO="oucs"

cd "/Users/hariomkumar/Desktop/0uCs Engine"

echo "=== Initializing git ==="
git init

echo "=== Adding all files ==="
git add .

echo "=== Creating commit ==="
git commit -m "feat: OUCS Engine v1.0.0 — Open Universal Container for Sound

- C core engine (liboucs) with full binary .oucs format
- Reed-Solomon error correction, AES-256-GCM encryption
- Selective unit-by-unit streaming (near-zero device load)
- Embedded metadata: BPM, key, mood, lyrics, waveform, fingerprint
- HTTP Range request support (CDN streaming)
- Python bindings (pip install oucs)
- JavaScript/WASM bindings (npm install oucs)
- Java/JNI bindings (Android + Desktop)
- CLI tools: pack, extract, info, merge, split, dedup, history, stream
- Public test UI: create.html + player.html
- MIT License"

echo "=== Setting main branch ==="
git branch -M main

echo "=== Adding remote ==="
git remote add origin "https://github.com/$USERNAME/$REPO.git"

echo "=== Pushing to GitHub ==="
echo "When prompted for password — paste your Personal Access Token (NOT your GitHub password)"
echo ""
git push -u origin main

echo ""
echo "=== Done! ==="
echo "Your repo: https://github.com/$USERNAME/$REPO"
