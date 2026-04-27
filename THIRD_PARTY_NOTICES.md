# Third-Party Notices

This repository contains source code for a standalone GGML C++ OmniVoice
inference runtime. It does not redistribute OmniVoice model weights.

## OmniVoice

- Project: https://github.com/k2-fsa/OmniVoice
- Model card: https://huggingface.co/k2-fsa/OmniVoice
- Paper: https://arxiv.org/abs/2604.00688
- License: Apache License 2.0, according to the upstream repository and model
  card.

The implementation in this repository is an independent C++/GGML runtime for
OmniVoice-compatible GGUF artifacts. Users must obtain model weights separately
and comply with the license terms and notices distributed with those artifacts.

## ggml

- Project: https://github.com/ggml-org/ggml
- Vendored path: `vendor/ggml`
- Baseline: tag `v0.9.11`, commit
  `49f84a924f6ea4fc2ef73dbbd8cc4d734b54bd6d`
- License: MIT License

The upstream ggml license is included in `vendor/ggml/LICENSE`.

This repository keeps `vendor/ggml` as an upstream submodule. OmniVoice's CUDA
Higgs decode path currently requires two project-local CUDA fixes. They are not
committed directly into the submodule; apply
`patches/ggml-v0.9.11-omnivoice-cuda-fixes.patch` after initializing the
submodule.

## Higgs Audio Tokenizer And Other Model Components

OmniVoice model artifacts may include or depend on components derived from the
Higgs Audio tokenizer and other upstream models. Those model artifacts are not
redistributed here.

If you distribute GGUF model files or derived weights, include the corresponding
model, tokenizer, and third-party license files from the model distributor.
The upstream OmniVoice Hugging Face discussion notes that the Higgs tokenizer
license should be shipped with tokenizer artifacts.

## Generated Audio

Generated audio may be subject to local laws, platform policies, and consent
requirements. Do not use this software for unauthorized voice cloning,
impersonation, fraud, or other illegal or unethical activity.
