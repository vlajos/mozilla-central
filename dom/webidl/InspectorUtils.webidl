/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 */
dictionary InspectorRGBTriple {
  /*
   * NOTE: Using octet for RGB components is not generally OK, because
   * they can be outside the 0-255 range, but for backwards-compatible
   * named colors (which is what we use this dictionary for) the 0-255
   * assumption is fine.
   */
  octet r = 0;
  octet g = 0;
  octet b = 0;
};
