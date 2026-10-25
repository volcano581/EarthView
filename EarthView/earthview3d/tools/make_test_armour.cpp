// Generates Data/models/test_armour.osgt (E3 review: exercise the model-file path in the demo).
// Not part of the build. Linux: g++ make_test_armour.cpp -o make_test_armour -losg -losgDB
//   ./make_test_armour ../../Data/models/test_armour.osgt
// Writes a tiny armour-like test model: hull box + turret + gun, front along +Y, origin on the ground.
#include <osg/Geode>
#include <osg/Geometry>
#include <osgDB/WriteFile>
static void box(osg::Vec3Array* v, osg::Vec4Array* c, osg::Vec3 lo, osg::Vec3 hi, osg::Vec4 col)
{
    const osg::Vec3 p[8] = {{lo.x(),lo.y(),lo.z()},{hi.x(),lo.y(),lo.z()},{hi.x(),hi.y(),lo.z()},{lo.x(),hi.y(),lo.z()},
                            {lo.x(),lo.y(),hi.z()},{hi.x(),lo.y(),hi.z()},{hi.x(),hi.y(),hi.z()},{lo.x(),hi.y(),hi.z()}};
    const int f[6][4] = {{0,3,2,1},{4,5,6,7},{0,1,5,4},{2,3,7,6},{1,2,6,5},{3,0,4,7}};
    const float shade[6] = {0.5f,1.0f,0.7f,0.85f,0.8f,0.6f};
    for (int i = 0; i < 6; ++i) {
        const int t[6] = {0,1,2,0,2,3};
        for (int k : t) { v->push_back(p[f[i][k]]); c->push_back(osg::Vec4(col.r()*shade[i], col.g()*shade[i], col.b()*shade[i], 1)); }
    }
}
int main(int, char** argv)
{
    auto* v = new osg::Vec3Array; auto* c = new osg::Vec4Array;
    const osg::Vec4 olive(0.45f, 0.55f, 0.3f, 1);
    box(v, c, {-1.8f,-3.7f,0.4f}, {1.8f,3.7f,1.8f}, olive);          // hull
    box(v, c, {-1.2f,-1.5f,1.8f}, {1.2f,1.3f,2.6f}, olive);          // turret
    box(v, c, {-0.15f,1.3f,2.05f}, {0.15f,6.5f,2.35f}, {0.3f,0.3f,0.3f,1}); // gun, points forward (+Y)
    auto* g = new osg::Geometry; g->setVertexArray(v);
    g->setColorArray(c, osg::Array::BIND_PER_VERTEX);
    g->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, v->size()));
    auto* geode = new osg::Geode; geode->addDrawable(g); geode->setName("earthview3d test armour");
    return osgDB::writeNodeFile(*geode, argv[1]) ? 0 : 1;
}
