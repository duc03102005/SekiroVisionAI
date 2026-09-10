"""Compact dense Wolf/Enemy detector, independent of temporal attack heads."""
import torch
from torch import nn

WIDTH, HEIGHT, STRIDE = 320, 192, 8
GRID_W, GRID_H = WIDTH // STRIDE, HEIGHT // STRIDE
ROLES = ("Wolf", "Enemy")


class Depthwise(nn.Module):
    def __init__(self, incoming, outgoing, stride=1, dilation=1):
        super().__init__()
        self.layers = nn.Sequential(
            nn.Conv2d(incoming, incoming, 3, stride, dilation, dilation=dilation,
                      groups=incoming, bias=False),
            nn.BatchNorm2d(incoming), nn.ReLU(),
            nn.Conv2d(incoming, outgoing, 1, bias=False),
            nn.BatchNorm2d(outgoing), nn.ReLU(),
        )

    def forward(self, value):
        return self.layers(value)


class RoleDetector(nn.Module):
    def __init__(self):
        super().__init__()
        self.backbone = nn.Sequential(
            nn.Conv2d(3, 24, 3, 2, 1, bias=False), nn.BatchNorm2d(24), nn.ReLU(),
            Depthwise(24, 40, 2), Depthwise(40, 64, 2),
            Depthwise(64, 80, dilation=1), Depthwise(80, 80, dilation=2),
            Depthwise(80, 80, dilation=3),
        )
        self.head = nn.Conv2d(80, 10, 1)
        with torch.no_grad():
            self.head.bias.zero_()
            self.head.bias[0] = self.head.bias[5] = -2.19
        yy, xx = torch.meshgrid(torch.arange(GRID_H), torch.arange(GRID_W), indexing="ij")
        self.register_buffer("grid_x", xx.float()[None, None])
        self.register_buffer("grid_y", yy.float()[None, None])

    def raw(self, frames):
        return self.head(self.backbone(frames)).reshape(-1, 2, 5, GRID_H, GRID_W)

    def forward(self, frames):
        prediction = self.raw(frames).sigmoid()
        scores = prediction[:, :, 0]
        cx = (prediction[:, :, 1] + self.grid_x) / GRID_W
        cy = (prediction[:, :, 2] + self.grid_y) / GRID_H
        width, height = prediction[:, :, 3], prediction[:, :, 4]
        boxes = torch.stack((cx-width/2, cy-height/2, cx+width/2, cy+height/2), dim=2).clamp(0, 1)
        return scores, boxes
