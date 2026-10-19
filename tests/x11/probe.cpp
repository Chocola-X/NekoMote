// Manual checks on an isolated display. Never injects keyboard or pointer input.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
unsigned char channel(unsigned long pixel, unsigned long mask)
{
    if (!mask)
        return 0;
    while (!(mask & 1))
    {
        pixel >>= 1;
        mask >>= 1;
    }
    return static_cast<unsigned char>((pixel & mask) * 255 / mask);
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc < 3)
            throw std::runtime_error("usage: probe shape|unmap|map|capture WINDOW_ID|root [FILE]");
        std::unique_ptr<Display, decltype(&XCloseDisplay)> display(XOpenDisplay(nullptr),
                                                                   XCloseDisplay);
        if (!display)
            throw std::runtime_error("cannot open diagnostic DISPLAY");
        const std::string id = argv[2];
        size_t consumed = 0;
        const Window window =
            id == "root" ? DefaultRootWindow(display.get()) : std::stoul(id, &consumed, 0);
        if (id != "root" && consumed != id.size())
            throw std::runtime_error("invalid explicit window id");
        const std::string operation = argv[1];
        if (operation == "shape")
        {
            int count = 0, ordering = 0;
            auto* rectangles =
                XShapeGetRectangles(display.get(), window, ShapeInput, &count, &ordering);
            std::cout << "input_rectangles=" << count << '\n';
            if (rectangles)
                XFree(rectangles);
        }
        else if (operation == "map")
            XMapWindow(display.get(), window);
        else if (operation == "unmap")
            XUnmapWindow(display.get(), window);
        else if (operation == "capture" && argc == 4)
        {
            XWindowAttributes attributes{};
            if (!XGetWindowAttributes(display.get(), window, &attributes))
                throw std::runtime_error("cannot query capture geometry");
            auto destroy_image = [](XImage* image) { XDestroyImage(image); };
            std::unique_ptr<XImage, decltype(destroy_image)> image(
                XGetImage(display.get(), window, 0, 0, static_cast<unsigned>(attributes.width),
                          static_cast<unsigned>(attributes.height), AllPlanes, ZPixmap),
                destroy_image);
            if (!image)
                throw std::runtime_error("cannot read diagnostic X11 pixels");
            std::ofstream stream(argv[3], std::ios::binary);
            stream << "P6\n" << attributes.width << ' ' << attributes.height << "\n255\n";
            std::vector<unsigned char> row(static_cast<size_t>(attributes.width) * 3);
            for (int y = 0; y < attributes.height; ++y)
            {
                for (int x = 0; x < attributes.width; ++x)
                {
                    const auto pixel = XGetPixel(image.get(), x, y);
                    const auto offset = static_cast<size_t>(x) * 3;
                    row[offset] = channel(pixel, image->red_mask);
                    row[offset + 1] = channel(pixel, image->green_mask);
                    row[offset + 2] = channel(pixel, image->blue_mask);
                }
                stream.write(reinterpret_cast<const char*>(row.data()),
                             static_cast<std::streamsize>(row.size()));
            }
            if (!stream)
                throw std::runtime_error("cannot write diagnostic capture");
        }
        else
            throw std::runtime_error("invalid diagnostic operation");
        XSync(display.get(), False);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
