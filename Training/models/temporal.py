"""Causal motion baselines sharing the native temporal-v2 contract.

FeatureProxyTCN uses appearance and frame-difference cells. These are explicitly
NOT detected objects, keypoints, weapon tracks, or camera-compensated optical flow.
"""
import math

import torch
from torch import nn
from torch.nn import functional as F

OUTPUT_NAMES_V1 = ["attack_probability", "threat_probability", "tti_ms",
                "tti_uncertainty_ms", "state_logits", "class_logits",
                "direction_logits"]
OUTPUT_NAMES = OUTPUT_NAMES_V1 + ["attack_direction_logits"]
ARCHITECTURES = ("cnn_gru", "cnn_tcn", "optical_flow_fusion", "feature_tcn", "video_transformer")


class FrameEncoder(nn.Module):
    def __init__(self, width=64):
        super().__init__()
        self.layers = nn.Sequential(
            nn.Conv2d(3, 16, 5, stride=4, padding=2), nn.ReLU(),
            nn.Conv2d(16, 16, 3, stride=2, padding=1, groups=16),
            nn.Conv2d(16, 32, 1), nn.ReLU(),
            nn.Conv2d(32, 32, 3, stride=2, padding=1, groups=32),
            nn.Conv2d(32, width, 1), nn.ReLU())

    def forward(self, frames):
        b, t, c, h, w = frames.shape
        return self.layers(frames.reshape(b*t, c, h, w)).mean((-2, -1)).reshape(b, t, -1)


class Heads(nn.Module):
    def __init__(self, width, contract="temporal-v2"):
        super().__init__()
        self.attack = nn.Linear(width, 1)
        self.threat = nn.Linear(width, 1)
        self.tti = nn.Linear(width, 2)
        self.state = nn.Linear(width, 9)
        self.attack_class = nn.Linear(width, 14)
        self.direction = nn.Linear(width, 5)
        self.attack_direction = nn.Linear(width, 8) if contract == "temporal-v2" else None
        self.register_buffer("attack_temperature", torch.ones(1))
        self.register_buffer("threat_temperature", torch.ones(1))

    def forward(self, features):
        timing = self.tti(features)
        # Laplace location/scale in milliseconds. A trained uncertainty head is
        # an estimate, not a calibrated coverage guarantee.
        result = (torch.sigmoid(self.attack(features) / self.attack_temperature),
                torch.sigmoid(self.threat(features) / self.threat_temperature),
                F.softplus(timing[:, :1]) * 1000.0,
                F.softplus(timing[:, 1:]) * 250.0 + 5.0,
                self.state(features), self.attack_class(features),
                self.direction(features))
        return result + (self.attack_direction(features),) if self.attack_direction is not None else result


class CNNGRU(nn.Module):
    def __init__(self, frames=16, width=64, contract="temporal-v2"):
        super().__init__()
        self.encoder = FrameEncoder(width)
        self.temporal = nn.GRU(width, width, batch_first=True)
        self.heads = Heads(width, contract)

    def encode(self, frames):
        return self.temporal(self.encoder(frames))[0][:, -1]

    def forward(self, frames):
        return self.heads(self.encode(frames))


class FeatureProxy(nn.Module):
    """Spatial appearance cells + strictly backward temporal differences."""
    width = 96

    def forward(self, frames):
        b, t, c, h, w = frames.shape
        cells = F.adaptive_avg_pool2d(frames.reshape(b*t, c, h, w), (4, 4))
        cells = cells.reshape(b, t, 48)
        previous = torch.cat((cells[:, :1], cells[:, :-1]), dim=1)
        return torch.cat((cells, cells-previous), dim=-1)


class CausalBlock(nn.Module):
    def __init__(self, width, dilation):
        super().__init__()
        self.left = 2 * dilation
        self.conv = nn.Conv1d(width, width, 3, dilation=dilation)
        self.mix = nn.Conv1d(width, width, 1)

    def forward(self, x):
        return x + self.mix(F.relu(self.conv(F.pad(x, (self.left, 0)))))


class FeatureProxyTCN(nn.Module):
    def __init__(self, frames=16, width=64, contract="temporal-v2"):
        super().__init__()
        self.encoder = FeatureProxy()
        self.project = nn.Linear(FeatureProxy.width, width)
        self.temporal = nn.Sequential(*(CausalBlock(width, d) for d in (1, 2, 4, 8, 16)))
        self.heads = Heads(width, contract)

    def encode(self, frames):
        return self.temporal(self.project(self.encoder(frames)).transpose(1, 2))[:, :, -1]

    def forward(self, frames):
        return self.heads(self.encode(frames))


class CNNTCN(nn.Module):
    """A learned spatial encoder followed by strictly left-padded convolutions."""
    def __init__(self, frames=16, width=64, contract="temporal-v2"):
        super().__init__()
        self.encoder = FrameEncoder(width)
        self.temporal = nn.Sequential(*(CausalBlock(width, d) for d in (1, 2, 4, 8, 16)))
        self.heads = Heads(width, contract)

    def encode(self, frames):
        return self.temporal(self.encoder(frames).transpose(1, 2))[:, :, -1]

    def forward(self, frames):
        return self.heads(self.encode(frames))


class CausalLucasKanade(nn.Module):
    """Dense local least-squares optical flow from the preceding observation.

    Solve the brightness-constancy normal equations over a 5x5 neighborhood at
    40x40 resolution. This is a small-displacement, single-level Lucas-Kanade
    estimate, not learned pose, a weapon tracker or large-motion ground truth.
    Texture confidence suppresses aperture/flat-region instability. Subtract
    spatial mean flow before feature fusion to remove uniform translation;
    rotation/parallax/zoom still require hard negatives. Only t-1 and t are read.
    All operators export inside ONNX, so native and training use the same flow.
    """
    def __init__(self, size=40):
        super().__init__()
        self.size = size
        self.register_buffer("luma", torch.tensor([0.299, 0.587, 0.114]).reshape(1, 3, 1, 1))
        self.register_buffer("dx", torch.tensor([[-0.5, 0., 0.5]]).reshape(1, 1, 1, 3))
        self.register_buffer("dy", torch.tensor([[-0.5], [0.], [0.5]]).reshape(1, 1, 3, 1))

    def forward(self, frames):
        b, t, c, h, w = frames.shape
        rgb = F.interpolate(frames.reshape(b*t, c, h, w), size=(self.size, self.size),
                            mode="bilinear", align_corners=False)
        gray = (rgb*self.luma).sum(dim=1, keepdim=True).reshape(b, t, 1, self.size, self.size)
        previous = torch.cat((gray[:, :1], gray[:, :-1]), dim=1)
        midpoint = ((gray+previous)*0.5).reshape(b*t, 1, self.size, self.size)
        ix = F.conv2d(F.pad(midpoint, (1, 1, 0, 0), mode="replicate"), self.dx)
        iy = F.conv2d(F.pad(midpoint, (0, 0, 1, 1), mode="replicate"), self.dy)
        it = (gray-previous).reshape_as(midpoint)
        moments = F.avg_pool2d(torch.cat((ix*ix, ix*iy, iy*iy, ix*it, iy*it), dim=1),
                              kernel_size=5, stride=1, padding=2)
        xx, xy, yy, xt, yt = (moments[:, i:i+1] for i in range(5))
        # Tikhonov regularization defines finite flow in low-texture areas.
        xx, yy = xx+1e-4, yy+1e-4
        determinant = (xx*yy-xy*xy).clamp_min(1e-8)
        u = ((xy*yt-yy*xt)/determinant).clamp(-4., 4.)
        v = ((xy*xt-xx*yt)/determinant).clamp(-4., 4.)
        return torch.cat((u, v), dim=1).reshape(b, t, 2, self.size, self.size)


class OpticalFlowFusion(nn.Module):
    def __init__(self, frames=16, width=64, contract="temporal-v2"):
        super().__init__()
        self.encoder = FrameEncoder(width)
        self.flow = CausalLucasKanade()
        self.flow_encoder = nn.Sequential(
            nn.Conv2d(6, 16, 3, stride=2, padding=1), nn.ReLU(),
            nn.Conv2d(16, 16, 3, stride=2, padding=1, groups=16),
            nn.Conv2d(16, width, 1), nn.ReLU())
        self.project = nn.Linear(width*2, width)
        self.temporal = nn.Sequential(*(CausalBlock(width, d) for d in (1, 2, 4, 8, 16)))
        self.heads = Heads(width, contract)

    def encode(self, frames):
        flow = self.flow(frames)
        residual = flow-flow.mean((-2, -1), keepdim=True)
        prior = torch.cat((residual[:, :1], residual[:, :-1]), dim=1)
        acceleration = residual-prior
        # Raw and residual flow retain the camera estimate while supplying
        # explicit velocity and temporal acceleration features to the model.
        motion = torch.cat((flow, residual, acceleration), dim=2)
        b, t, c, h, w = motion.shape
        motion = self.flow_encoder(motion.reshape(b*t, c, h, w)).mean((-2, -1)).reshape(b, t, -1)
        features = self.project(torch.cat((self.encoder(frames), motion), dim=-1))
        return self.temporal(features.transpose(1, 2))[:, :, -1]

    def forward(self, frames):
        return self.heads(self.encode(frames))


class CausalAttention(nn.Module):
    def __init__(self, width, frames, heads=4):
        super().__init__()
        self.head_count = heads
        self.head_width = width // heads
        self.norm = nn.LayerNorm(width)
        self.qkv = nn.Linear(width, 3 * width)
        self.out = nn.Linear(width, width)
        self.ff = nn.Sequential(nn.LayerNorm(width), nn.Linear(width, 2*width),
                                nn.ReLU(), nn.Linear(2*width, width))
        self.register_buffer("mask", torch.triu(torch.full((frames, frames), -10000.0), diagonal=1))

    def forward(self, x):
        b, t, width = x.shape
        qkv = self.qkv(self.norm(x)).reshape(b, t, 3, self.head_count, self.head_width)
        q, k, v = qkv[:, :, 0].transpose(1, 2), qkv[:, :, 1].transpose(1, 2), qkv[:, :, 2].transpose(1, 2)
        attention = (q @ k.transpose(-1, -2)) / math.sqrt(self.head_width)
        attention = torch.softmax(attention + self.mask[:t, :t], dim=-1)
        x = x + self.out((attention @ v).transpose(1, 2).reshape(b, t, width))
        return x + self.ff(x)


class VideoTransformer(nn.Module):
    def __init__(self, frames=16, width=64, contract="temporal-v2"):
        super().__init__()
        self.encoder = FrameEncoder(width)
        self.position = nn.Parameter(torch.zeros(1, frames, width))
        nn.init.normal_(self.position, std=0.02)
        self.temporal = nn.Sequential(CausalAttention(width, frames), CausalAttention(width, frames))
        self.heads = Heads(width, contract)

    def encode(self, frames):
        encoded = self.encoder(frames)
        return self.temporal(encoded + self.position[:, :encoded.shape[1]])[:, -1]

    def forward(self, frames):
        return self.heads(self.encode(frames))


def build_model(architecture="cnn_gru", frames=16, width=64, contract="temporal-v2"):
    if frames not in (8, 16, 24, 32, 48):
        raise ValueError("frames must be 8, 16, 24, 32 or 48")
    if width < 16 or width % 4:
        raise ValueError("width must be >=16 and divisible by4")
    if contract not in ("temporal-v1", "temporal-v2"):
        raise ValueError("Unsupported temporal contract")
    constructors = dict(zip(ARCHITECTURES, (CNNGRU, CNNTCN, OpticalFlowFusion, FeatureProxyTCN, VideoTransformer)))
    if architecture not in constructors:
        raise ValueError(f"Unknown architecture {architecture!r}")
    return constructors[architecture](frames=frames, width=width, contract=contract)
