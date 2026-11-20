"""Produce source-only restoration inputs for the original Hospital atmosphere textures."""
import hashlib
import json

import numpy as np
from PIL import Image


LUMA = np.array((0.2126, 0.7152, 0.0722), dtype=np.float32)
METHODS = {
    "MHOsfx_xbox_rayonlum.png": ("beam", 4, "Wrapped linear-light interpolation and low-strength source-dither reduction while preserving soft panning beams."),
    "MHOtex_xbox_Envmapvitre.png": ("optical", 8, "Bounded linear-light interpolation of existing soft optical lobes, with no edge sharpening."),
    "Prison_TEX_Tsky04.png": ("gradient", 8, "Mean of identical horizontal scan lines with subpixel interpolation of the original vertical gradient."),
    "Prison_TEX_Tsky05.png": ("flat", 8, "Exact flat original RGB (2, 2, 12), with no generated detail."),
    "Prison_TEX_lune.png": ("masked detail", 8, "Bounded linear-light crater contrast recovery within the original Moon mask."),
    "Prison_TEX_mh_buildingblack.png": ("flat", 8, "Exact zero-albedo black field, preventing invented skyline lighting or marks."),
    "Prison_TEX_starsky.png": ("stars", 8, "Flux-conserving linear point interpolation, with original star positions and colours."),
    "Prison_TEX_warp_skyline1.png": ("masked detail", 8, "Low-strength source detail recovery on the existing lit windows and horizon with the source translucency intact."),
    "Prison_TEX_warp_skyline_sol.png": ("detail", 8, "Bounded contrast recovery for authored building patterns and lights."),
}


def linear(x):
    return np.where(x <= 0.04045, x/12.92, ((x+0.055)/1.055)**2.4)


def gamma(x):
    x = np.clip(x, 0, 1)
    return np.where(x <= 0.0031308, x*12.92, 1.055*x**(1/2.4)-0.055)


def resize(x, size, filter, wrapping=False):
    if wrapping:
        padding = 4
        factor = size[0] // x.shape[1]
        expanded = np.pad(x, ((padding, padding), (padding, padding), (0,0)), mode="wrap")
        distance = padding*factor
        extended = resize(expanded, (size[0]+2*distance, size[1]+2*distance), filter)
        return extended[distance:-distance,distance:-distance]
    return np.stack([np.asarray(Image.fromarray(x[...,i]).resize(size, filter), dtype=np.float32)
                     for i in range(x.shape[2])], axis=-1)


def gaussian(x, wrapping=False):
    # Symmetric seven-tap filter over float linear radiance, with wrap at scrolling boundaries.
    weights = np.array([1,6,15,20,15,6,1], dtype=np.float32)/64.0
    for axis in (0,1):
        p = [(0,0)]*3
        p[axis] = (3,3)
        padded = np.moveaxis(np.pad(x, p, mode="wrap" if wrapping else "edge"), axis, 0)
        count = x.shape[axis]
        x = np.moveaxis(sum(w*padded[i:i+count] for i,w in enumerate(weights)),0,axis)
    return x


def restore(stock, alpha, method, factor, name):
    h,w,_ = stock.shape
    size = (w*factor,h*factor)
    if method == "flat":
        if not np.all(stock == stock[0,0]):
            raise ValueError("Nonconstant input tagged as a flat field")
        return np.broadcast_to(stock[0,0],(size[1],size[0],3)).copy()
    if method == "beam":
        mostly_old = stock*0.77 + gaussian(stock, wrapping=True)*0.23
        return np.clip(resize(mostly_old,size,Image.Resampling.BICUBIC,wrapping=True),
                       0,stock.max(axis=(0,1)))
    if method == "optical":
        return np.clip(resize(stock,size,Image.Resampling.BICUBIC),stock.min(axis=(0,1)),stock.max(axis=(0,1)))
    if method == "gradient":
        horizontal = stock.mean(axis=1, keepdims=True)
        return resize(horizontal,size,Image.Resampling.BILINEAR)
    if method == "stars":
        return resize(stock,size,Image.Resampling.BILINEAR,wrapping=True)
    base = resize(stock,size,Image.Resampling.LANCZOS)
    if method == "masked detail":
        # Crop contrast recovery to the interior where alpha denotes a solid source surface.
        blend = resize(np.clip(alpha[...,None],0,1),size,Image.Resampling.BILINEAR)
    else:
        blend = 1.0
    detail = resize(stock-gaussian(stock),size,Image.Resampling.BICUBIC)
    # Avoid a new horizon edge or a bright ring on the Moon by limiting gain and respecting source highlights.
    amount = 0.19 if name == "Prison_TEX_lune.png" else 0.15
    return np.clip(base + blend*detail*amount,stock.min(axis=(0,1)),stock.max(axis=(0,1)))


def match_channel_means(result, src, weight):
    weighted = weight[...,None]
    total = max(float(weight.sum()),1)
    target = (src*weighted).sum(axis=(0,1),dtype=np.float64)/total
    rgb = result.astype(np.float32).copy()
    for _ in range(4):
        mean = (rgb*weighted).sum(axis=(0,1),dtype=np.float64)/total
        multiplier = np.divide(target,mean,out=np.ones(3),where=mean>1e-10)
        rgb = np.clip(rgb*multiplier.astype(np.float32),0,1)
    return rgb, target


def run(source, stage):
    manifest = {}
    qa = {}
    for name,(method,scale,description) in METHODS.items():
        path = source/name
        with Image.open(path) as src:
            original = np.array(src.convert("RGBA"))
            height,width = original.shape[:2]
        stock = linear(original[...,:3].astype(np.float32)/255)
        alpha = original[...,3].astype(np.float32)/255
        result = restore(stock,alpha,method,scale,name)
        color_reference = np.repeat(np.repeat(stock,scale,axis=0),scale,axis=1)
        alpha_bytes = np.repeat(np.repeat(original[...,3],scale,axis=0),scale,axis=1)
        weight = alpha_bytes.astype(np.float32)/255
        result,target = match_channel_means(result,color_reference,weight)
        srgb = np.rint(gamma(result)*255).astype(np.uint8)
        rgba = np.dstack((srgb,alpha_bytes))
        image_path = stage/"Color"/name
        image_path.parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(rgba).save(image_path)
        readback = np.array(Image.open(image_path).convert("RGBA"))
        assert np.array_equal(readback[...,3], alpha_bytes)
        if method == "flat" and not np.all(readback == original[0,0]):
            raise ValueError("Flat restoration introduced a visual change")
        measured = (linear(readback[...,:3].astype(np.float32)/255)*weight[...,None]).sum(axis=(0,1),dtype=np.float64)/max(float(weight.sum()),1)
        mean_drift = float((measured-target)@LUMA)
        if abs(mean_drift) > 0.001:
            raise ValueError(f"{name}: source-weighted linear luminance drift {mean_drift}")
        stock_image = Image.fromarray(original[...,:3]).resize((width*scale,height*scale),Image.Resampling.BICUBIC)
        stock_cubic = np.asarray(stock_image,dtype=np.float32)
        qa[name] = {
            "source_sha256":hashlib.sha256(path.read_bytes()).hexdigest(),
            "source_px":[width,height],"restored_px":[width*scale,height*scale],"process":description,
            "alpha_exact_nearest": True,
            "source_weighted_luma_linear":round(float(target@LUMA),7),
            "restored_weighted_luma_linear":round(float(measured@LUMA),7),
            "weighted_luma_drift_linear":round(mean_drift,7),
            "luma_mad_vs_gamma_bicubic_255":round(float(np.abs((srgb-stock_cubic)@LUMA).mean()),4),
            "flat_values_preserved": method == "flat",
        }
        manifest[name] = image_path.relative_to(stage).as_posix()
    (stage/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n", encoding="utf-8")
    (stage/"OriginalAtmosphereQA.json").write_text(json.dumps(qa,indent=2)+"\n", encoding="utf-8")
    print(f"Restored {len(qa)} original atmosphere PNGs under {stage}")
