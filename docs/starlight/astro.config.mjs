import { defineConfig } from "astro/config";
import starlight from "@astrojs/starlight";

export default defineConfig({
  integrations: [
    starlight({
      title: "Thagore Developer Docs",
      description: "Code-truth documentation for Thagore compiler, language, runtime, and ecosystem.",
      sidebar: [
        {
          label: "Start Here",
          autogenerate: { directory: "." }
        }
      ]
    })
  ]
});
