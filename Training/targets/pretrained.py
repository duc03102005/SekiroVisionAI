"""ImageNet features with a newly supervised gameplay role detection head.

The ImageNet classifier is discarded. No natural-image class is relabeled Wolf
or enemy; the new two-role head learns only the explicit gameplay annotations.
"""
import torch
from torch import nn
from torch.nn import functional as F
from Training.targets.model import RoleDetector, GRID_H, GRID_W


class PretrainedRoleDetector(RoleDetector):
    def __init__(self, pretrained=True):
        # Reuse the output-coordinate contract, without initializing the tiny
        # backbone or consuming its random initialization before this experiment.
        nn.Module.__init__(self)
        from torchvision.models import mobilenet_v3_small, MobileNet_V3_Small_Weights
        weights = MobileNet_V3_Small_Weights.IMAGENET1K_V1 if pretrained else None
        self.backbone = mobilenet_v3_small(weights=weights).features
        self.lateral8 = nn.Conv2d(24, 64, 1)
        self.lateral16 = nn.Conv2d(48, 64, 1)
        self.lateral32 = nn.Conv2d(576, 64, 1)
        self.fusion = nn.Sequential(nn.Conv2d(64, 64, 3, padding=1), nn.ReLU(),
                                    nn.Conv2d(64, 64, 3, padding=1), nn.ReLU())
        self.head = nn.Conv2d(64, 10, 1)
        with torch.no_grad():
            self.head.bias.zero_(); self.head.bias[0] = self.head.bias[5] = -2.19
        yy, xx = torch.meshgrid(torch.arange(GRID_H), torch.arange(GRID_W), indexing="ij")
        self.register_buffer("grid_x", xx.float()[None, None]); self.register_buffer("grid_y", yy.float()[None, None])
        self.register_buffer("mean", torch.tensor([0.485, 0.456, 0.406])[None, :, None, None])
        self.register_buffer("std", torch.tensor([0.229, 0.224, 0.225])[None, :, None, None])

    def raw(self, frames):
        feature = (frames-self.mean)/self.std
        feature8 = feature16 = None
        for index, layer in enumerate(self.backbone):
            feature = layer(feature)
            if index == 3:
                feature8 = feature
            elif index == 8:
                feature16 = feature
        fused = self.lateral8(feature8) + F.interpolate(self.lateral16(feature16), size=(GRID_H, GRID_W), mode="nearest")
        fused = fused + F.interpolate(self.lateral32(feature), size=(GRID_H, GRID_W), mode="nearest")
        return self.head(self.fusion(fused)).reshape(-1, 2, 5, GRID_H, GRID_W)

    def train(self, mode=True):
        super().train(mode)
        # Retain pretrained running statistics with this small correlated seed.
        for layer in self.backbone.modules():
            if isinstance(layer, nn.BatchNorm2d):
                layer.eval()
        return self
