#!/usr/bin/env bash
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="${APP_DIR}/.venv-d3qn"

python3 -m venv "${VENV_DIR}"
"${VENV_DIR}/bin/python" -m pip install --upgrade pip
"${VENV_DIR}/bin/python" -m pip install torch --index-url https://download.pytorch.org/whl/cpu
"${VENV_DIR}/bin/python" -m pip install \
  gym==0.25.2 \
  "numpy<2" \
  networkx \
  tqdm \
  tensorboard \
  openpyxl \
  pyserial \
  matplotlib

echo "D3QN environment ready: ${VENV_DIR}"
echo "Use: PYTHONPATH='app/广播组网上位机/app' ${VENV_DIR}/bin/python -m pc_d3qn_cli.main train"
