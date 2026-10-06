"""Decode a local paired SSX FG-input capture and test simple full-frame alignment.

No image estimates are fed to DLSS. Residuals include HUD, filtering, gamma and
any late effects; this is a diagnostic, not a validated UI-alpha extractor.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image

p = argparse.ArgumentParser()
p.add_argument('capture', type=Path)
args = p.parse_args()
root = args.capture
m = json.loads((root/'metadata.json').read_text())
sw, sh, fw, fh = [m[k] for k in ('source_width','source_height','final_width','final_height')]
src = np.fromfile(root/'post-effects.bin', dtype=np.uint8).reshape(sh,sw,4).astype(np.float32)/255
selectors = [(m['source_swizzle'] >> (i*3)) & 7 for i in range(4)]
if max(selectors) >= 4 or m['source_signs'] != 0:
    raise ValueError('Unimplemented source swizzle/sign interpretation')
src = src[:,:,selectors[:3]]
packed = np.fromfile(root/'final-guest.bin', dtype='<u4').reshape(fh,fw)
dst = np.stack([(packed >> (i*10)) & 1023 for i in range(3)],axis=2).astype(np.float32)/1023
Image.fromarray(np.rint(src*255).astype(np.uint8)).save(root/'post-effects.png')
Image.fromarray(np.rint(dst*255).astype(np.uint8)).save(root/'final-guest.png')

yy, xx = np.mgrid[fh//5:fh*4//5:8,fw//5:fw*4//5:8]
ref = dst[yy,xx]
gamma = None
if 'gamma_ramp' in m:
    if m['gamma_pwl']:
        raise ValueError('This alignment test currently supports the 256-entry display table only')
    ramp = np.asarray(m['gamma_ramp'],dtype=np.uint32)
    if len(ramp)!=256:
        raise ValueError('Invalid display gamma table length')
    gamma = np.stack([(ramp >> shift) & 1023 for shift in (20,10,0)],axis=1)/1023
def sample(dx,dy):
    x = (xx+.5)*sw/fw-.5+dx
    y = (yy+.5)*sh/fh-.5+dy
    x0 = np.floor(x).astype(int); y0 = np.floor(y).astype(int)
    ax = (x-x0)[...,None]; ay = (y-y0)[...,None]
    x1=np.clip(x0+1,0,sw-1); y1=np.clip(y0+1,0,sh-1)
    x0=np.clip(x0,0,sw-1); y0=np.clip(y0,0,sh-1)
    filtered = ((src[y0,x0]*(1-ax)+src[y0,x1]*ax)*(1-ay)+
                (src[y1,x0]*(1-ax)+src[y1,x1]*ax)*ay)
    if gamma is None:
        return filtered
    # The guest full-screen pass writes 8-bit color before IssueSwap looks it
    # up in the display table. Match that quantization, never infer exposure.
    codes = np.floor(np.clip(filtered,0,1)*255+.5).astype(int)
    return gamma[codes,np.arange(3)]
best = None
for dy in np.arange(-2,2.01,.25):
    for dx in np.arange(-2,2.01,.25):
        predicted=sample(dx,dy)
        per_pixel=np.max(np.abs(predicted-ref),axis=2)
        # Central sample, trimmed to reduce HUD influence. Still report untrimmed
        # quantiles; a best fit alone cannot prove a correct full-screen mapping.
        score=float(np.mean(np.sort(per_pixel.ravel())[:int(per_pixel.size*.8)]))
        if best is None or score < best['trimmed_max_channel_error']:
            best=dict(source_pixel_offset=[float(dx),float(dy)],trimmed_max_channel_error=score,
                      error_quantiles=np.quantile(per_pixel,[.5,.9,.95,.99]).tolist(),
                      fraction_within_one_8bit_code=float(np.mean(per_pixel<=1/255)),
                      mean_rgb_ratio=float(predicted.mean()/ref.mean()))
report=dict(guest_frame=m['guest_frame'],selectors=selectors,display_gamma_applied=gamma is not None,
            full_frame_resize_fit=best,
            scope='One post-effects source and final guest image; no gameplay FG evaluation or inferred UI alpha.')
(root/'alignment.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
