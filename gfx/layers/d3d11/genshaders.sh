# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

tempfile=tmpShaderHeader
rm CompositorD3D11Shaders.h
fxc CompositorD3D11.fx -ELayerQuadVS -nologo -Tvs_4_0_level_9_3 -Fh$tempfile -VnLayerQuadVS
cat $tempfile >> CompositorD3D11Shaders.h
fxc CompositorD3D11.fx -ESolidColorShader -Tps_4_0_level_9_3 -nologo -Fh$tempfile -VnSolidColorShader
cat $tempfile >> CompositorD3D11Shaders.h
fxc CompositorD3D11.fx -ERGBShader -Tps_4_0_level_9_3 -nologo -Fh$tempfile -VnRGBShader
cat $tempfile >> CompositorD3D11Shaders.h
fxc CompositorD3D11.fx -ERGBAShader -Tps_4_0_level_9_3 -nologo -Fh$tempfile -VnRGBAShader
cat $tempfile >> CompositorD3D11Shaders.h
fxc CompositorD3D11.fx -EYCbCrShader -Tps_4_0_level_9_3 -nologo -Fh$tempfile -VnYCbCrShader
cat $tempfile >> CompositorD3D11Shaders.h
