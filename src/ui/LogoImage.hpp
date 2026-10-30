// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

namespace xythos {

// The logo in the top bar, in two layers of the same size (WebP bytes with alpha, decodeWebp()): the crop circle in
// white, tinted with the highlight colour when drawn, and the wordmark. LogoImage.cpp is generated from resources/logo/
// by tools/embed_logo.py.
extern const unsigned char kLogoMark[];
extern const unsigned int kLogoMarkSize;
extern const unsigned char kLogoWordmark[];
extern const unsigned int kLogoWordmarkSize;

} // namespace xythos
