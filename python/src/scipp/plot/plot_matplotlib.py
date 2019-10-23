# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2019 Scipp contributors (https://github.com/scipp)
# @file
# @author Neil Vaytet

# Scipp imports
from . import config
from .tools import edges_to_centers, centers_to_edges, axis_label, \
                   parse_colorbar, get_1d_axes, axis_to_dim_label

# Other imports
import numpy as np
import matplotlib.pyplot as plt


def plot_1d(input_data, logx=False, logy=False, logxy=False,
            color=None, filename=None, title=None, axes=None, mpl_axes=None):
    """
    Plot a 1D spectrum.
    Input is a dictionary containing a list of DataProxy.
    If the coordinate of the x-axis contains bin edges, then a bar plot is
    made.
    """

    fig = None
    if mpl_axes is not None:
        ax = mpl_axes
    else:
        fig, ax = plt.subplots(1, 1)

    out = {"fill_between": {}, "step": {}, "line": {},
           "errorbar": {}}

    for i, (name, var) in enumerate(input_data.items()):

        xlab, ylab, x, y = get_1d_axes(var, axes, name)

        # Check for bin edges
        if x.shape[0] == y.shape[0] + 1:
            xe = x.copy()
            ye = np.concatenate(([0], y))
            x = edges_to_centers(x)
            out["fill_between"][name] = ax.fill_between(
                xe, ye, step="pre", alpha=0.6, label=name, color=color[i])
            out["step"][name] = ax.step(xe, ye, color=color[i])
        else:
            out["line"][name] = ax.plot(x, y, label=name, color=color[i])
        # Include variance if present
        if var.variances is not None:
            out["errorbar"][name] = ax.errorbar(x, y,
                                                yerr=np.sqrt(var.variances),
                                                linestyle='None',
                                                ecolor=color[i])

    ax.set_xlabel(xlab)
    ax.set_ylabel(ylab)
    ax.legend()
    if title is not None:
        ax.set_title(title)
    if logx or logxy:
        ax.set_xscale("log")
    if logy or logxy:
        ax.set_yscale("log")

    out["ax"] = ax
    if fig is not None:
        out["fig"] = fig

    return out


def plot_2d(input_data, name=None, axes=None, contours=False, cb=None,
            filename=None, show_variances=False, mpl_axes=None, mpl_cax=None,
            **kwargs):
    """
    Plot a 2D image.
    If countours=True, a filled contour plot is produced, if False, then a
    standard image made of pixels is created.
    """

    if input_data.variances is None and show_variances:
        raise RuntimeError("The supplied data does not contain variances.")

    if axes is None:
        axes = input_data.dims

    # Get coordinates axes and dimensions
    zdims = input_data.dims
    nz = input_data.shape

    dimx, labx, xcoord = axis_to_dim_label(input_data, axes[-1])
    dimy, laby, ycoord = axis_to_dim_label(input_data, axes[-2])
    xy = [xcoord.values, ycoord.values]

    # Check for bin edges
    dims = [xcoord.dims[0], ycoord.dims[0]]
    shapes = [xcoord.shape[0], ycoord.shape[0]]
    # Find the dimension in z that corresponds to x and y
    transpose = (zdims[0] == dims[0]) and (zdims[1] == dims[1])
    idx = np.roll([1, 0], int(transpose))

    grid_edges = [None, None]
    grid_centers = [None, None]
    for i in range(2):
        if shapes[i] == nz[idx[i]]:
            grid_edges[i] = centers_to_edges(xy[i])
            grid_centers[i] = xy[i]
        elif shapes[i] == nz[idx[i]] + 1:
            grid_edges[i] = xy[i]
            grid_centers[i] = edges_to_centers(xy[i])
        else:
            raise RuntimeError("Dimensions of x Coord ({}) and Value ({}) do "
                               "not match.".format(shapes[i], nz[idx[i]]))

    # Parse colorbar
    cbar = parse_colorbar(config.cb, cb)

    # Get or create matplotlib axes
    fig = None
    if mpl_axes is not None:
        ax = mpl_axes
    else:
        fig, ax = plt.subplots(1, 1 + show_variances)
    if mpl_cax is not None:
        cax = mpl_cax
    else:
        cax = [None] * (1 + show_variances)

    # Make sure axes are stored in arrays
    try:
        _ = len(ax)
    except TypeError:
        ax = [ax]
    try:
        _ = len(cax)
    except TypeError:
        cax = [cax]

    # Update axes labels
    for a in ax:
        a.set_xlabel(axis_label(xcoord, name=labx))
        a.set_ylabel(axis_label(ycoord, name=laby))

    params = {"values": {"cbmin": "min", "cbmax": "max", "cblab": name}}
    if show_variances:
        params["variances"] = {"cbmin": "min_var", "cbmax": "max_var",
                               "cblab": "variances"}

    out = {}

    for i, (key, param) in enumerate(sorted(params.items())):
        # if param is not None:
        arr = getattr(input_data, key)
        if cbar["log"]:
            with np.errstate(invalid="ignore", divide="ignore"):
                arr = np.log10(arr)
        if cbar[param["cbmin"]] is not None:
            vmin = cbar[param["cbmin"]]
        else:
            vmin = np.amin(arr[np.where(np.isfinite(arr))])
        if cbar[param["cbmax"]] is not None:
            vmax = cbar[param["cbmax"]]
        else:
            vmax = np.amax(arr[np.where(np.isfinite(arr))])

        if transpose:
            arr = arr.T

        args = {"vmin": vmin, "vmax": vmax, "cmap": cbar["name"]}
        if contours:
            img = ax[i].contourf(grid_centers[0], grid_centers[1], arr, **args)
        else:
            img = ax[i].imshow(arr, extent=[grid_edges[0][0],
                                            grid_edges[0][-1],
                                            grid_edges[1][0],
                                            grid_edges[1][-1]],
                               origin="lower", aspect="auto", **args)
        c = plt.colorbar(img, ax=ax[i], cax=cax[i])
        c.ax.set_ylabel(axis_label(var=input_data, name=param["cblab"],
                                   log=cbar["log"]))

        out[key] = {"ax": ax[i], "cb": c, "img": img}

    if fig is not None:
        out["fig"] = fig

    return out
