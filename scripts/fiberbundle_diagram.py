import sys
import numpy as np
from xml.dom.minidom import parse

import matplotlib.pyplot as plt
import matplotlib.patches as patches
from matplotlib.patches import Polygon
from matplotlib.lines import Line2D 
from matplotlib.ticker import MaxNLocator
# import matplotlib.cm as cm
# import matplotlib.colors as colors
from matplotlib import rc
#rc('font',**{'family':'sans-serif','sans-serif':['Helvetica']})
# for Palatino and other serif fonts use:
rc('font',**{'family':'serif','serif':['Palatino']})
rc('text', usetex=True)
rc('font', family='serif', size=30)

def GetFiberType(tbase, tbundle):
  ftype=""
  print(tbase, tbundle)
  if tbundle[0] == "R":
    if tbase[0] == "R":
      r1 = int(tbundle[1])
      r2 = int(tbase[1])
      ftype = "R"+str(int(r1-r2))
  elif tbundle == "SE3":
    if tbase == "R3":
      ftype = "SO3"
  elif tbundle == "SE2":
    if tbase == "R2":
      ftype = "SO2"
  elif tbundle == "SE2RN":
    if tbase == "R2":
      ftype = "SO2RN"
    elif tbase == "SE2":
      ftype = "RN"
  elif tbundle == "SE3RN":
    if tbase == "R3":
      ftype = "SO3RN"
    elif tbase == "SE3":
      ftype = "RN"
  else:
    print("Could not determine fiber type")
    print(tbase, tbundle)
    sys.exit(0)

  if ftype=="":
    print("Could not determine fiber type")
    print(tbase, tbundle)
    sys.exit(0)

  return ftype


def typeToLatex(typespace):
  if typespace[0] == "R":
    rn = int(typespace[1])
    length = rn
    text = r'$\mathbb{R}^{'+str(rn)+'}$'
  elif typespace == "SE2RN":
    rn = 6
    length = rn + 3
    text = r'$SE(2)\times \mathbb{R}^{'+str(rn)+'}$'
  elif typespace == "SE2":
    length = 3
    text = r'$SE(2)$'
  elif typespace == "SO2":
    length = 1
    text = r'$SO(2)$'
  elif typespace == "SE3":
    length = 6
    text = r'$SE(3)$'
  elif typespace == "SO3":
    length = 3
    text = r'$SO(3)$'
  else:
    print("not knowing type",typespace)
    sys.exit(0)
  return [length, text]


def PlotFiberDiagram(fname, fontsize, ystep, xstep):
  doc = parse(fname)
  hierarchy = doc.getElementsByTagName("hierarchy")[0]
  levels = hierarchy.getElementsByTagName("level")

  fig = plt.figure(0)
  fig.patch.set_facecolor('white')
  ax = plt.gca()

  height = 0.5*ystep

  Nx = 0

  class RectangleSpace(object):

    def __init__(self, rid, typespace, sid):
      self._dashedOnly = False
      self._rid = rid
      self._typespace = typespace
      self._sid = sid
      #print(rid, typespace, sid)
      
      [self._length, self._text] = typeToLatex(typespace)


    def setXY(self, x, y, xstep, ystep):
      self._x = x
      self._y = y
      self._xstep = xstep
      self._ystep = ystep
      # print(self._x, self._y)

    def SetPatch(self, ax):
      offset = 0.05*self._length*self._xstep
      tx = self._x + 0.4*self._length*self._xstep - offset
      ty = self._y + 0.3*height

      if self._sid:
        x1 = self._x
        x2 = x1 + self._length*self._xstep
        x3 = x1 + self._fulllength*self._xstep
        y1 = self._y
        y2 = y1 - (ystep - height)
        length_x = (self._fulllength-self._length)*self._xstep
        if self._dashedOnly:
          x2 = x1
          length_x = self._length*self._xstep

        rect = patches.Rectangle(
                (x2,y1), length_x, height,
                linestyle="--", linewidth=2, fill=False, edgecolor='k')
        ax.add_patch(rect)

        if length_x > 0:
          [tmp, self._fibertext] = typeToLatex(self._fibertype)
          tx2 = x2 + 0.4*length_x - offset
          if self._fibertype[0] == "S":
            tx2 = x2 + 0.2*length_x - offset
          ax.text(tx2, ty, self._fibertext, {'color': 'k', 'fontsize': fontsize})

        # if self._fulllength > self._length or self._dashedOnly:
        # if not self._dashedOnly:
        if self._dashedOnly:
          x1 = x2
          x2 = x3
        xx = [x1, x1]
        yy = [y1, y2]
        line = Line2D(xx, yy, linewidth=2, linestyle="--", color='k')
        ax.add_line(line)
        xx = [x2, x2]
        line = Line2D(xx, yy, linewidth=2, linestyle="--", color='k')
        ax.add_line(line)

        poly = Polygon([[x1, y1], [x1, y2], [x2, y2], [x2, y1]],
          closed=True, fill=True, linewidth=0, color=(0.95,0.95,0.95))
        ax.add_patch(poly)

      if not self._dashedOnly:
        rect = patches.Rectangle(
                (self._x,self._y), self._length*self._xstep, height,
                linewidth=2, fill=True, edgecolor='k', facecolor=(0.9, 0.9, 0.9))
        ax.add_patch(rect)

        ax.text(tx, ty, self._text, {'color': 'k', 'fontsize': fontsize})


    def setDashed(self):
      self._dashedOnly = True


  rectangleSpaces = []
  y = 0.0
  dx = 0
  dy = (len(levels)-1)*ystep+height

  rectanglesLastLevel = []
  for level in levels[::-1]:
    robots = level.getElementsByTagName("robot")

    x = 0.0

    Nr = len(robots)
    for robot in robots:
      rid = int(robot.getAttribute("id"))
      t = str(robot.getAttribute("type"))
      sid = robot.getAttribute("simplification_of_id")
      rect = RectangleSpace(rid, t, sid)
      rect.setXY(x, y, xstep, ystep)

      if sid:
        sid = int(sid)
        for rectLL in rectanglesLastLevel:
          if sid == rectLL._rid:
            rect._x = rectLL._x
            rect._fulllength = rectLL._length
            rect._fibertype = GetFiberType(rect._typespace, rectLL._typespace)

      rectangleSpaces.append(rect)
      x = x + (rect._length * xstep)

    if len(robots) < len(rectanglesLastLevel):
      print("Adding Additional Robots")
      for rectLL in rectanglesLastLevel:
        found = False
        for robot in robots:
          sid = robot.getAttribute("simplification_of_id")
          if sid:
            sid = int(sid)
          if rectLL._rid == sid:
            found = True
            break
        if not found:
          print("Robot",rectLL._rid,"projects to empty set")
          rect = RectangleSpace(rectLL._rid, rectLL._typespace, rectLL._rid)
          rect._fulllength = rectLL._length
          rect.setXY(rectLL._x, y, rectLL._xstep, rectLL._ystep)
          rect.setDashed()
          rect._fibertype = rectLL._typespace
          rectangleSpaces.append(rect)
          Nr = Nr+1

    if x>dx:
      dx = x

    y = y + ystep

    rectanglesLastLevel.clear()
    for k in range(1,Nr+1):
      rectanglesLastLevel.append(rectangleSpaces[-k])

    print("Added Rectangles:")
    for rect in rectanglesLastLevel:
      print(rect._rid,"->",rect._sid)

  for rect in rectangleSpaces:
    rect.SetPatch(ax)

  plt.axis([0,dx,0,dy])
  plt.tight_layout()
  ax.set_aspect('equal')
  plt.axis('off')
  ax.get_xaxis().set_visible(False)
  ax.get_yaxis().set_visible(False)
  plt.savefig(fname+".png", bbox_inches='tight', pad_inches=0)
  plt.clf()
  plt.cla()
  plt.close()
  # plt.show()


# PlotFiberDiagram('../data/experiments/WAFR2020/06D_bhattacharya_square.xml', 30, 20, 5)
PlotFiberDiagram('../data/experiments/WAFR2020/12D_drones_tree.xml', 25, 20, 8)
# PlotFiberDiagram('../data/experiments/WAFR2020/18D_manipulators_crossing.xml', 20, 20, 5)