"""Missing, censored and unreviewed labels never become negative targets."""
import torch
from torch.nn import functional as F


def masked_mean(loss, mask):
    return (loss.reshape(-1)*mask.reshape(-1)).sum()/mask.sum().clamp_min(1.0)


def multitask_loss(outputs, labels, masks):
    attack, threat, tti, uncertainty, state, attack_class, direction = outputs[:7]
    losses = {}
    for name, predicted in (("attack", attack), ("threat", threat)):
        losses[name] = masked_mean(F.binary_cross_entropy(predicted.reshape(-1).clamp(1e-6, 1-1e-6),
                                                         labels[name].reshape(-1), reduction="none"), masks[name])
    for name, predicted in (("state", state), ("class", attack_class), ("direction", direction)):
        losses[name] = masked_mean(F.cross_entropy(predicted, labels[name], reduction="none"), masks[name])
    # Laplace NLL in seconds avoids an arbitrary millisecond scale in the log.
    scale = uncertainty.reshape(-1)/1000
    error = (tti.reshape(-1)-labels["tti"].reshape(-1)).abs()/1000
    losses["tti"] = masked_mean(error/scale + torch.log(2*scale), masks["tti"])
    total = losses["attack"] + 1.5*losses["threat"] + 0.35*losses["state"] + 0.35*losses["class"] + 0.1*losses["direction"] + 0.4*losses["tti"]
    if len(outputs) == 8:
        losses["attack_direction"] = masked_mean(
            F.cross_entropy(outputs[7], labels["attack_direction"], reduction="none"), masks["attack_direction"])
        total = total + 0.25*losses["attack_direction"]
    return total, losses
