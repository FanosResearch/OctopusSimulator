// dump_steps.cpp -- export slam_demo's data for slam_steps.py.
//
// Includes slam_demo.cpp unchanged, runs it twice on the host (no deadlines,
// then real time with --period-us, default 30) and writes steps.json:
// walls, true poses, every scan, both runs' estimates / tracked flags /
// keyframes / timestamps, and the final grid of the no-deadline run.
//
// build: make -C .. steps   (or g++ -O2 -std=c++20 -DNO_M5 dump_steps.cpp -pthread)
// usage: ./dump_steps [period_us] [out.json]

#define main demo_main
#include "../slam_demo.cpp"
#undef main

#include <string>

namespace {

void
reset_run()
{
    walls.clear(); gt.clear(); map_us.clear(); map_t0.clear(); map_t1.clear();
    released = 0; fe_done = 0; kf_tail = 0; kf_head = 0; ready = 0;
    fe_exit = false; aggr_stop = false; lost = 0; map_backlog = 0;
}

void
put_poses(FILE *f, const char *name, const std::vector<Pose> &v)
{
    fprintf(f, "\"%s\":[", name);
    for (size_t i = 0; i < v.size(); i++)
        fprintf(f, "%s[%.17g,%.17g,%.17g]", i ? "," : "", v[i].x, v[i].y, v[i].th);
    fprintf(f, "],");
}

template <class T>
void
put_list(FILE *f, const char *name, const std::vector<T> &v, uint64_t base = 0)
{
    fprintf(f, "\"%s\":[", name);
    for (size_t i = 0; i < v.size(); i++) {
        if constexpr (std::is_same_v<T, uint64_t>)
            fprintf(f, "%s%.3f", i ? "," : "", v[i] ? (v[i] - base) / 1000.0 : -1.0);
        else
            fprintf(f, "%s%d", i ? "," : "", int(v[i]));
    }
    fprintf(f, "],");
}

/* One run's results, times in us from the ROI start (-1 = never). */
void
put_run(FILE *f, const char *name)
{
    fprintf(f, "\"%s\":{", name);
    put_poses(f, "est", est);
    put_list(f, "tracked", tracked);
    std::vector<int> kfs(kf_scan.begin(), kf_scan.begin() + kf_tail.load());
    put_list(f, "kf_scan", kfs);
    put_poses(f, "kf_pose", std::vector<Pose>(kf_pose.begin(), kf_pose.begin() + kf_tail.load()));
    put_list(f, "rel_us", rel_ns, roi_t0);
    put_list(f, "fe_t0_us", fe_t0, roi_t0);
    put_list(f, "fe_t1_us", fe_t1, roi_t0);
    put_list(f, "map_t0_us", map_t0, roi_t0);
    put_list(f, "map_t1_us", map_t1, roi_t0);
    fprintf(f, "\"period_us\":%g,\"offline\":%d,\"lost\":%d}", period_us, int(offline), lost);
}

} // namespace

int
main(int argc, char **argv)
{
    const char *period = argc > 1 ? argv[1] : "30";
    const char *out = argc > 2 ? argv[2] : "steps.json";
    FILE *f = fopen(out, "w");
    if (!f) {
        perror(out);
        return 1;
    }
    fprintf(f, "{\"W\":%g,\"H\":%g,\"inset\":%g,\"res\":%g,\"beams\":%d,\"range\":%g,",
            kW, kH, kInset, kRes, kBeams, kRange);

    FILE *keep = stdout;
    stdout = fopen("/dev/null", "w");
    const char *a_off[] = {"x", "--offline"};
    demo_main(2, (char **)a_off);
    stdout = keep;

    fprintf(f, "\"gw\":%d,\"gh\":%d,\"walls\":[", gw, gh);
    for (size_t i = 0; i < walls.size(); i++)
        fprintf(f, "%s[%.17g,%.17g,%.17g,%.17g]", i ? "," : "",
                walls[i].x0, walls[i].y0, walls[i].x1, walls[i].y1);
    fprintf(f, "],");
    put_poses(f, "gt", gt);
    fprintf(f, "\"sensor\":[");
    for (size_t i = 0; i < sensor.size(); i++)
        fprintf(f, "%s%.9g", i ? "," : "", sensor[i]);
    fprintf(f, "],\"grid_final\":[");
    for (size_t i = 0; i < size_t(gw) * gh; i++)
        fprintf(f, "%s%d", i ? "," : "", int(grid[i].load()));
    fprintf(f, "],");
    put_run(f, "offline");
    fprintf(f, ",");

    reset_run();
    offline = false;
    stdout = fopen("/dev/null", "w");
    const char *a_rt[] = {"x", "--period-us", period};
    demo_main(3, (char **)a_rt);
    stdout = keep;
    put_run(f, "late");
    fprintf(f, "}\n");
    fclose(f);
    printf("wrote %s (late run: period %s us)\n", out, period);
    return 0;
}
