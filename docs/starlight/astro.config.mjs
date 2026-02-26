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
          items: [{ label: "Home", link: "/" }]
        },
        { label: "Philosophy", autogenerate: { directory: "philosophy" } },
        {
          label: "Install & Tooling",
          items: [
            { label: "Quick Start", link: "/install/quick-start/" },
            { label: "Updating Thagore", link: "/install/update/" },
            { label: "CLI Reference", link: "/install/cli-reference/" },
            { label: "Release & Installers", link: "/install/release-installers/" },
          ]
        },
        { label: "Syntax", autogenerate: { directory: "syntax" } },
        { label: "Compiler", autogenerate: { directory: "compiler" } },
        { label: "Standard Library", autogenerate: { directory: "stdlib" } },
        { label: "Patterns", autogenerate: { directory: "patterns" } },
        { label: "Integration", autogenerate: { directory: "integration" } },
        { label: "Memory", autogenerate: { directory: "memory" } },
        { label: "Type System", autogenerate: { directory: "type-system" } },
        { label: "Reference", autogenerate: { directory: "reference" } },
        { label: "Contributing", autogenerate: { directory: "contributing" } }
      ]
    })
  ]
});
