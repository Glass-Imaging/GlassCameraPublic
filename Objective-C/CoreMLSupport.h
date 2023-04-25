//
//  CoreMLSupport.h
//  GlassCamera
//
//  Created by Fabio Riccardi on 4/24/23.
//

#ifndef CoreMLSupport_h
#define CoreMLSupport_h

void fmenApplyToImage(const gls::image<gls::luma_pixel_16>& rawImage, int whiteLevel, gls::image<gls::rgba_pixel_fp16>* processedImage);

#endif /* CoreMLSupport_h */
