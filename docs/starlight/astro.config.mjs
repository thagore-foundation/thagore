import { defineConfig } from "astro/config";
import starlight from "@astrojs/starlight";

const default_site = "https://docs.thagore.org";
const default_base = "/";
const site = process.env.DOCS_SITE || default_site;
const base = process.env.DOCS_BASE || default_base;

export default defineConfig({
  site,
  base,
  integrations: [
    starlight({
      title: "Thagore Developer Docs",
      description: "Code-truth documentation for Thagore compiler, language, runtime, and ecosystem.",
      sidebar: [
        {
          label: "Overview",
          items: [
            { label: "Home", link: "/" },
            { label: "Install", link: "/install/" },
          ]
        },
        {
          label: "Language",
          items: [
            { label: "Overview", link: "/language/overview/" },
            { label: "Syntax", link: "/language/syntax/" },
            { label: "Types", link: "/language/types/" },
            { label: "Functions", link: "/language/functions/" },
            { label: "Structs", link: "/language/structs/" },
            { label: "Control Flow", link: "/language/control/" },
            { label: "Imports", link: "/language/imports/" },
            { label: "Constants", link: "/language/constants/" },
          ]
        },
        {
          label: "Standard Library",
          items: [
            { label: "Overview", link: "/stdlib/" },
            { label: "math", link: "/stdlib/math/" },
            { label: "io", link: "/stdlib/io/" },
            { label: "string", link: "/stdlib/string/" },
            { label: "vec", link: "/stdlib/vec/" },
            { label: "sort", link: "/stdlib/sort/" },
            { label: "collections", link: "/stdlib/collections/" },
            { label: "iter", link: "/stdlib/iter/" },
            { label: "fmt", link: "/stdlib/fmt/" },
            { label: "convert", link: "/stdlib/convert/" },
            { label: "time", link: "/stdlib/time/" },
          ]
        },
        {
          label: "Drago",
          items: [
            { label: "Overview", link: "/drago/" },
            { label: "Commands", link: "/drago/commands/" },
            { label: "Manifest", link: "/drago/manifest/" },
            { label: "Workspaces", link: "/drago/workspaces/" },
          ]
        },
        {
          label: "Competitive Programming",
          items: [
            { label: "Overview", link: "/cp/" },
            { label: "Sorting", link: "/cp/sorting/" },
            { label: "Graph", link: "/cp/graph/" },
            { label: "Dynamic Programming", link: "/cp/dp/" },
            { label: "Math", link: "/cp/math/" },
          ]
        },
        { label: "Philosophy", autogenerate: { directory: "philosophy" } },
        { label: "Compiler", autogenerate: { directory: "compiler" } },
        { label: "Integration", autogenerate: { directory: "integration" } },
        {
          label: "Legacy Reference",
          items: [
            { label: "Quick Start", link: "/install/quick-start/" },
            { label: "Updating Thagore", link: "/install/update/" },
            { label: "CLI Reference", link: "/install/cli-reference/" },
            { label: "Release & Installers", link: "/install/release-installers/" },
            { label: "Old Syntax Guide", link: "/syntax/" },
            { label: "Patterns", link: "/patterns/" },
            { label: "Memory", link: "/memory/managed-string-runtime/" },
            { label: "Type System", link: "/type-system/static-typing-model/" },
            { label: "Reference", link: "/reference/api-reference-basic/" },
          ]
        },
        { label: "Contributing", autogenerate: { directory: "contributing" } }
      ]
    })
  ]
});
