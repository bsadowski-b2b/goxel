
import * as std from 'std'

/*
 * Example of file format support in js:
 */
/*
goxel.registerFormat({
  name: 'Test',
  exts: ['*.test'],
  exts_desc: 'test',
  import: function(img, path) {
    let layer = img.addLayer()
    let volume = layer.volume
    volume.setAt([0, 0, 0], [255, 0, 0, 255])
  },
  export: function(img, path) {
    try {
      console.log(`Save ${path}`)
      let out = std.open(path, 'w')
      let volume = img.getLayersVolume()
      volume.iter(function(p, c) {
        out.printf(`${p.x} ${p.y}, ${p.z} => ${c.r}, ${c.g}, ${c.b}\n`)
      })
      out.close()
      console.log('done')
    } catch(e) {
      console.log('error', e)
    }
  },
})
*/

function getRandomColor() {
  return [
    Math.floor(Math.random() * 256),
    Math.floor(Math.random() * 256),
    Math.floor(Math.random() * 256),
    255
  ]
}

goxel.registerScript({
  name: 'FillRandom',
  description: 'Fill selection with random voxels',
  onExecute: function() {
    let box = goxel.image.selectionBox
    let volume = goxel.image.activeLayer.volume
    box.iterVoxels(function(pos) {
      volume.setAt(pos, getRandomColor())
    })
  }
})

goxel.registerScript({
  name: 'Dilate',
  onExecute: function() {
    let volume = goxel.image.activeLayer.volume
    volume.copy().iter(function(pos, color) {
      for (let z = -1; z < 2; z++) {
        for (let y = -1; y < 2; y++) {
          for (let x = -1; x < 2; x++) {
            volume.setAt([pos.x + x, pos.y + y, pos.z + z], color)
          }
        }
      }
    })
  }
})

goxel.registerScript({
  name: 'VoxelSphere',
  description: 'Create a sphere in the selection box with the selected color',
  onExecute: function() {
    let box = goxel.image.selectionBox
    let volume = goxel.image.activeLayer.volume

    // First calculate bounds of selection box.
    let min = { x: Infinity, y: Infinity, z: Infinity }
    let max = { x: -Infinity, y: -Infinity, z: -Infinity }

    box.iterVoxels(function(pos) {
      min.x = Math.min(min.x, pos.x)
      min.y = Math.min(min.y, pos.y)
      min.z = Math.min(min.z, pos.z)
      max.x = Math.max(max.x, pos.x)
      max.y = Math.max(max.y, pos.y)
      max.z = Math.max(max.z, pos.z)
    })

    let center = {
      x: (min.x + max.x) / 2,
      y: (min.y + max.y) / 2,
      z: (min.z + max.z) / 2
    }

    let rx = (max.x - min.x) / 2
    let ry = (max.y - min.y) / 2
    let rz = (max.z - min.z) / 2
    let color = goxel.palette.color
    let sphereVoxels = 0

    box.iterVoxels(function(pos) {
      let dx = (pos.x - center.x) / rx
      let dy = (pos.y - center.y) / ry
      let dz = (pos.z - center.z) / rz
      let distSq = dx * dx + dy * dy + dz * dz

      if (distSq <= 1.0) {
        volume.setAt(pos, color)
        sphereVoxels++
      }
    })

    if (sphereVoxels === 0)
      console.log('WARNING: No voxels were selected!')
    else
      console.log(`Filled sphere created with ${sphereVoxels} voxels`)
  }
})
